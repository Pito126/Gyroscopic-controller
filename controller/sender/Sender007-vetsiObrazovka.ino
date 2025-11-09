#include <Wire.h> // Komunikace I2C pro OLED a gyroskop
#include <MPU6050.h> // Gyroskop
#include <esp_now.h> // WI-FI pro esp 32
#include <WiFi.h> // Zapnutí WI-FI modů
#include <Adafruit_GFX.h> // Funkce OLED displeje
#include <Adafruit_SH110X.h> // Nastavení displeje
#include <math.h> // Matematické funke

// --- OLED nastavení ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BUTTON_PIN 23
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- MPU6050 ---
MPU6050 mpu;

// --- Struktura pro ESP-NOW ---
typedef struct struct_message {
  char state[3]; // např. "F1", "R3"
} struct_message;

struct_message myMessage;

// --- MAC adresa přijímače ---
uint8_t receiverAddress[] = {0xC0, 0x5D, 0x89, 0xCE, 0x33, 0x34};

// --- Proměnné ---
float pitchFiltered = 0;
float rollFiltered = 0;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 300; // Interval po kterém se odesílají hodnoty (ms)
char lastState[3] = "X";

// --- Baterie ---
const int BAT_PIN = 34; // Pin pro detekci analogového signálu
const float R1 = 216000.0; // Hodnota R1
const float R2 = 100000.0; // Hodnota R2
const float ADC_REF = 3.3;
const int ADC_RES = 4095;
float batteryVoltage = 0.0; // Napětí baterie
int batteryPercent = 0; // Hodnota baterie
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 10000;
const float kalibracniFaktor = 1.097;
const float posun = 0.0;

// --- OLED stránky ---
int currentScreen = 0;
bool lastButtonReading = HIGH;

// --- Funkce pro tlačítko ---
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading == LOW && lastButtonReading == HIGH) {
    currentScreen++;
    if (currentScreen > 2) currentScreen = 0;
    Serial.print("Přepínám na stránku: ");
    Serial.println(currentScreen);
    delay(250);
  }
  lastButtonReading = reading;
}

// --- Reinicializace MPU ---
void reinitMPU() {
  Serial.println("Reinicializuji MPU...");
  mpu.initialize();
  delay(200);
}

// --- Měření baterie ---
void updateBattery() {
  const int SAMPLES = 10;
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(BAT_PIN);
    delay(5);
  }
  float avgADC = sum / (float)SAMPLES;
  float vOut = (avgADC * ADC_REF) / ADC_RES;
  batteryVoltage = vOut / (R2 / (R1 + R2));
  batteryVoltage = batteryVoltage * kalibracniFaktor + posun;
  if (batteryVoltage >= 8.4) batteryPercent = 100;
  else if (batteryVoltage <= 6.0) batteryPercent = 0;
  else batteryPercent = (batteryVoltage - 6.0) * (100.0 / (8.4 - 6.0));
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(200);

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 nenalezen!");
    while (1);
  }
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
  Serial.println("MPU6050 OK");

  // --- OLED SH1106 inicializace ---
  if (!display.begin(0x3C, true)) { // true = reset display
    Serial.println("SH1106 OLED nenalezen!");
    while (1);
  }
  display.display();
  delay(1000);
  display.clearDisplay();

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

  Serial.println("Inicializace dokončena");
  updateBattery();
  lastBatteryUpdate = millis();
}

void loop() {
  handleButton();

  if (millis() - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    updateBattery();
    lastBatteryUpdate = millis();
  }

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  if (abs(ax) > 32000 || abs(ay) > 32000 || abs(az) > 32000) {
    reinitMPU();
    return;
  }

  float fax = ax / 16384.0f;
  float fay = ay / 16384.0f;
  float faz = az / 16384.0f;
  if (isnan(fax) || isnan(fay) || isnan(faz)) {
    reinitMPU();
    return;
  }

  float pitch = atan2(fax, sqrt(fay * fay + faz * faz)) * 180.0f / PI;
  float roll  = atan2(fay, sqrt(fax * fax + faz * faz)) * 180.0f / PI;
  pitchFiltered = 0.9f * pitchFiltered + 0.1f * pitch;
  rollFiltered  = 0.9f * rollFiltered + 0.1f * roll;

  char state[3] = "I"; // Idle
  float p = pitchFiltered;
  float r = rollFiltered;

  // --- Dopředu (Forward) ---
  if (p < -10 && p >= -20) strcpy(state, "F1");
  else if (p < -20 && p >= -30) strcpy(state, "F2");
  else if (p < -30 && p >= -40) strcpy(state, "F3");
  else if (p < -40) strcpy(state, "F4");
  // --- Dozadu (Backward) ---
  else if (p > 10 && p <= 20) strcpy(state, "B1");
  else if (p > 20 && p <= 30) strcpy(state, "B2");
  else if (p > 30 && p <= 40) strcpy(state, "B3");
  else if (p > 40) strcpy(state, "B4");
  // --- Doleva (Left) – konstantní rychlost ---
  else if (r < -10) strcpy(state, "L4");
  // --- Doprava (Right) – konstantní rychlost ---
  else if (r > 10) strcpy(state, "R4");

  if (strcmp(state, lastState) != 0 || millis() - lastSend >= SEND_INTERVAL) {
    strcpy(myMessage.state, state);
    esp_now_send(receiverAddress, (uint8_t*)&myMessage, sizeof(myMessage));
    Serial.print("Odeslán stav: ");
    Serial.println(state);
    strcpy(lastState, state);
    lastSend = millis();
  }

  display.clearDisplay();
  if (currentScreen == 0) {
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2;
    int radius = 30;
    float scale = 1.5;
    float offsetX = rollFiltered * scale;
    float offsetY = pitchFiltered * scale;
    float distance = sqrt(offsetX * offsetX + offsetY * offsetY);
    if (distance > radius - 3) {
      offsetX = offsetX * (radius - 3) / distance;
      offsetY = offsetY * (radius - 3) / distance;
    }
    int dotX = centerX + (int)offsetX;
    int dotY = centerY + (int)offsetY;
    display.drawCircle(centerX, centerY, radius, SH110X_WHITE);
    display.fillCircle(dotX, dotY, 3, SH110X_WHITE);
  } else if (currentScreen == 1) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("== SENZOR DATA ==");
    display.print("Pitch: "); display.println(pitchFiltered, 1);
    display.print("Roll : "); display.println(rollFiltered, 1);
    display.print("Stav : "); display.println(state);
    display.print("Baterie: "); display.print(batteryVoltage, 2);
    display.print("V ("); display.print(batteryPercent); display.println("%)");
  } else if (currentScreen == 2) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("== INFO ==");
    display.println("Autor: Dominik Klein");
    display.println("Verze: 1.8");
    display.println("Maturitni projekt");
    display.println("SPSUL 2026");
  }
  display.display();
  delay(20);
}
