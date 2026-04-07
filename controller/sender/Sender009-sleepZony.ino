#include <Wire.h>            // Komunikace I2C pro OLED a gyroskop
#include <MPU6050.h>         // Gyroskop
#include <esp_now.h>         // WI-FI pro esp 32
#include <WiFi.h>            // Zapnutí WI-FI modů
#include <Adafruit_GFX.h>    // Funkce OLED displeje
#include <Adafruit_SH110X.h> // Nastavení displeje
#include <math.h>            // Matematické funkce
#include <esp_sleep.h>       // Light sleep

// --- OLED nastavení ---
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define OLED_RESET    -1
#define BUTTON_PIN    23     // Light sleep funguje na libovolném GPIO
#define LONG_PRESS_MS 5000   // Doba držení pro uspání (ms)

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- MPU6050 ---
MPU6050 mpu;

// --- Struktura zprávy pro ESP-NOW ---
// Definice struktury stejná jako na příjimači
typedef struct struct_message {
  char state[3]; // Řetěazec stavu, např. "F1", "R4", "I"
} struct_message;

struct_message myMessage; // Strukturovaná zpráva připravená k odesílání

// --- MAC adresa přijímače ---
uint8_t receiverAddress[] = {0xC0, 0x5D, 0x89, 0xCE, 0x33, 0x8C};

// --- Proměnné gyroskopu ---
float pitchFiltered = 0;
float rollFiltered  = 0;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 300; // ms
char lastState[3] = "X";

// --- Baterie ovladače ---
const int   BAT_PIN     = 34;
const float R1          = 100000.0;
const float R2          = 220000.0; 
const float ADC_REF     = 3.3; // Referenční napětí převodníku
const int   ADC_RES     = 4095; // Rozlišení ADC
const float BAT_MAX     = 4.2; // Maximální napětí baterie
const float BAT_MIN     = 3.0; // Minimální napětí baterie
float batteryVoltage    = 0.0; // Napětí baterie
int   batteryPercent    = 0; // Procenta baterie
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 10000;

// --- OLED stránky ---
int  currentScreen     = 0;
bool lastButtonReading = HIGH;

void reinitMPU();


// Funkce pro uspání ESP32 spotřeba 0.8 místo 107 mA
void goToSleep() {
  Serial.println("Uspavam do light sleep...");

  // Zpráva na displeji
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display.setCursor(20, 20);
  display.println("  Uspavam...");
  display.setCursor(4, 36);
  display.println("Stiskni pro buzeni");
  display.fillCircle(48, 52, 2, SH110X_WHITE);
  display.fillCircle(62, 52, 2, SH110X_WHITE);
  display.fillCircle(76, 52, 2, SH110X_WHITE);
  display.display();
  delay(1000);

  // Vypni OLED
  display.clearDisplay();
  display.display();

  // Ukonči ESP-NOW a WiFi
  esp_now_deinit();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Probuzení přes GPIO 23 (LOW = stisk při INPUT_PULLUP)
  gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  Serial.println("Jdu spat...");
  delay(100);
  esp_light_sleep_start();

  // --- Po probuzení pokračuje kód zde ---
  Serial.println("Probuzen z light sleep");

  // Počkej na puštění tlačítka
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);
  delay(50);

  // Znovu inicializuj WiFi a ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba ESP-NOW reinit!");
  } else {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("ESP-NOW reinicializovan");
  }

  // Znovu inicializuj MPU
  reinitMPU();

  // Zapni OLED
  display.begin(0x3C, true);
  display.clearDisplay();
  display.display();

  Serial.println("Plny provoz obnoven");
}

// Stiknutí tlačítka přepne další obrazovku, delší podržení hodí tlačítko do sleepu
void handleButton() {
  static unsigned long pressStart = 0;
  static bool longPressFired      = false;

  bool reading = digitalRead(BUTTON_PIN);

  // Tlačítko právě stisknuto
  if (reading == LOW && lastButtonReading == HIGH) {
    pressStart     = millis();
    longPressFired = false;
  }

  // Kontrola long pressu
  if (reading == LOW && !longPressFired) {
    if (millis() - pressStart >= LONG_PRESS_MS) {
      longPressFired = true;
      goToSleep();
    }
  }

  // Krátký stisk = přepni obrazovku
  if (reading == HIGH && lastButtonReading == LOW && !longPressFired) {
    currentScreen++;
    if (currentScreen > 2) currentScreen = 0;
    Serial.print("Prepinám na stranku: ");
    Serial.println(currentScreen);
    delay(250);
  }

  lastButtonReading = reading;
}

// --- Funkce: reinicializace MPU ---
void reinitMPU() {
  Serial.println("Reinicializuji MPU...");
  mpu.initialize();
  delay(200);
}

// --- Funkce: měření baterie ---
void updateBattery() {
  const int SAMPLES = 10;

  analogSetPinAttenuation(BAT_PIN, ADC_11db);

  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(BAT_PIN);
    delay(5);
  }
  float avgADC = sum / (float)SAMPLES;

  // 3.78 / 1.310 = 2.885
  batteryVoltage = (avgADC / 4095.0f) * 3.9f * 2.885f;

  if      (batteryVoltage >= BAT_MAX) batteryPercent = 100;
  else if (batteryVoltage <= BAT_MIN) batteryPercent = 0;
  else batteryPercent = (int)((batteryVoltage - BAT_MIN) * (100.0f / (BAT_MAX - BAT_MIN)));
}

// -----------------------------------------------------------------------
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(200);

  // --- I2C Scanner ---
  Serial.println("\n--- I2C Scanner ---");
  byte error, address;
  int nDevices = 0;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C zarizeni nalezeno na adrese 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    } else if (error == 4) {
      Serial.print("Neznamy error na adrese 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0) Serial.println("Zadna I2C zarizeni nenalezena");
  else               Serial.println("Scanner hotovo");

  // --- Inicializace MPU6050 ---
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 nenalezen!");
    while (1);
  }
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
  Serial.println("MPU6050 OK");

  // --- Inicializace OLED ---
  if (!display.begin(0x3C, true)) {
    Serial.println("SH1106 OLED nenalezen!");
    while (1);
  }
  display.display();
  delay(1000);
  display.clearDisplay();

  // --- ESP-NOW setup ---
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba ESP-NOW init!");
    while (1);
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Inicializace dokoncena");

  updateBattery();
  lastBatteryUpdate = millis();
}

// -----------------------------------------------------------------------
void loop() {
  handleButton();

  // Aktualizace baterie
  if (millis() - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    updateBattery();
    lastBatteryUpdate = millis();
  }

// Načtení surových 16bitových hodnot ze tří os akcelerometru
int16_t ax, ay, az;
mpu.getAcceleration(&ax, &ay, &az);
// Ochrana proti přetečení – pokud hodnota překročí 32000, senzor se reinicializuje
if (abs(ax) > 32000 || abs(ay) > 32000 || abs(az) > 32000) {
    reinitMPU();
    return;
}
// Převod surových hodnot na jednotky g
float fax = ax / 16384.0f;
float fay = ay / 16384.0f;
float faz = az / 16384.0f;
// Kontrola neplatných hodnot NaN – při výskytu se senzor reinicializuje
if (isnan(fax) || isnan(fay) || isnan(faz)) {
    reinitMPU();
    return;
}

  // Výpočet úhlů z akcelerometru (bez filtrace)
  float pitch = atan2(fax, sqrt(fay * fay + faz * faz)) * 180.0f / PI;
  float roll  = atan2(fay, sqrt(fax * fax + faz * faz)) * 180.0f / PI;

  // Filtrace, zajištění protichybovosti
  pitchFiltered = 0.9f * pitchFiltered + 0.1f * pitch;
  rollFiltered  = 0.9f * rollFiltered  + 0.1f * roll;


  char state[3] = "I";
  float p = pitchFiltered;
  float r = rollFiltered;

  // 1. Extrémní diagonála = idle
  if (abs(p) > 80 && abs(r) > 40) {
    strcpy(state, "I");

  // 2. Dead zone: ±15° v obou osách = zastavení
  } else if (abs(p) < 15 && abs(r) < 15) {
    strcpy(state, "I");

  // 3. Pitch dominuje = dopředu nebo dozadu
  } else if (abs(p) > abs(r)) {
    if (p < 0) {
      if      (p > -35) strcpy(state, "F1");
      else if (p > -55) strcpy(state, "F2");
      else if (p > -75) strcpy(state, "F3");
      else              strcpy(state, "F4");
    } else {
      if      (p < 35)  strcpy(state, "B1");
      else if (p < 55)  strcpy(state, "B2");
      else if (p < 75)  strcpy(state, "B3");
      else              strcpy(state, "B4");
    }

  // 4. Roll dominuje = zatáčení
  } else {
    if      (r < -15) strcpy(state, "R4");
    else if (r >  15) strcpy(state, "L4");
  }

  // --- Odeslání přes ESP-NOW ---
  if (strcmp(state, lastState) != 0 || millis() - lastSend >= SEND_INTERVAL) {
    strcpy(myMessage.state, state);
    esp_now_send(receiverAddress, (uint8_t*)&myMessage, sizeof(myMessage));
    Serial.print("Odeslan stav: ");
    Serial.println(state);
    strcpy(lastState, state);
    lastSend = millis();
  }

  // Displej
  display.clearDisplay();

  // ── OBRAZOVKA 0 - HUD
  if (currentScreen == 0) {

    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2;
    int radius  = 28;
    float scale = 1.5;

    float offsetX  = rollFiltered  * scale;
    float offsetY  = pitchFiltered * scale;
    float distance = sqrt(offsetX * offsetX + offsetY * offsetY);
    if (distance > radius - 3) {
      offsetX = offsetX * (radius - 3) / distance;
      offsetY = offsetY * (radius - 3) / distance;
    }
    int dotX = centerX + (int)offsetX;
    int dotY = centerY + (int)offsetY;

    // Referenční kruh
    display.drawCircle(centerX, centerY, radius, SH110X_WHITE);

    // Přerušované křížové osy
    for (int x = centerX - radius + 2; x <= centerX + radius - 2; x++)
      if (x % 2 == 0) display.drawPixel(x, centerY, SH110X_WHITE);
    for (int y = centerY - radius + 2; y <= centerY + radius - 2; y++)
      if (y % 2 == 0) display.drawPixel(centerX, y, SH110X_WHITE);

    // Popisky směrů
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(centerX - 2, 0);                display.print("F");
    display.setCursor(centerX - 2, 57);               display.print("B");
    display.setCursor(2,             centerY - 3);    display.print("L");
    display.setCursor(SCREEN_WIDTH - 7, centerY - 3); display.print("R");

    // Pohyblivý bod
    display.fillCircle(dotX, dotY, 3, SH110X_WHITE);

    // Stavový badge vpravo nahoře
    display.drawRect(100, 1, 27, 11, SH110X_WHITE);
    display.setCursor(103, 3);
    display.print(state);

    // Mini ukazatel baterie vlevo nahoře
    display.drawRect(1, 1, 18, 7, SH110X_WHITE);
    display.drawRect(19, 3, 2, 3, SH110X_WHITE);
    int barW = map(batteryPercent, 0, 100, 0, 16);
    display.fillRect(2, 2, barW, 5, SH110X_WHITE);

  // ── OBRAZOVKA 1 – diagnostika
} else if (currentScreen == 1) {

  // Nastavení velikosti a barvy textu
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Nastavení počáteční pozice kurzoru
  display.setCursor(0, 0);

  // Nadpis obrazovky
  display.println("== SENZOR DATA ==");

  // Zobrazení vypočteného náklonu kolem osy X
  display.print("Pitch: ");
  display.println(pitchFiltered, 1);

  // Zobrazení náklonu kolem osy Y
  display.print("Roll : ");
  display.println(rollFiltered, 1);

  // Zobrazení aktuálního stavu řízení (např. F1, B2, L4...)
  display.print("Stav : ");

  display.println(state);

  // Zobrazení napětí baterie
  display.print("Baterie: ");
  display.print(batteryVoltage, 2);

  // Zobrazení procentuálního stavu baterie
  display.print("V (");
  display.print(batteryPercent);
  display.println("%)");

} 
  //── OBRAZOVKA 2 – informace o projektu
  else if (currentScreen == 2) {

  display.setTextSize(1);
  display.setCursor(0, 0);

  // Zobrazení základních informací o projektu
  display.println("== INFO ==");
  display.println("Autor: Dominik Klein");
  display.println("Verze: 2.1");
  display.println("Maturitni projekt");
  display.println("SPSUL 2026");
  }
  display.display();
  delay(20);
}
