#include <Wire.h>                 // I2C komunikace pro MPU6050 a OLED
#include <MPU6050.h>              // Knihovna pro gyroskop MPU6050
#include <esp_now.h>              // Knihovna pro ESP-NOW komunikaci
#include <WiFi.h>                 // Knihovna pro WiFi, potřebná pro ESP-NOW
#include <Adafruit_GFX.h>         // Základní grafika pro OLED
#include <Adafruit_SSD1306.h>     // Knihovna pro konkrétní OLED
#include <math.h>                 // Matematické funkce (atan2, sqrt, PI)

// --- OLED nastavení --- // Změnit na druhej oled
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BUTTON_PIN 23
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- MPU6050 ---
MPU6050 mpu;

// --- Stránky OLED ---
int currentScreen = 0;
bool lastButtonReading = HIGH;

// --- Struktura pro ESP-NOW ---
typedef struct struct_message {
  char state;
} struct_message;

struct_message myMessage;

// --- MAC adresa přijímače ---
uint8_t receiverAddress[] = {0xC0, 0x5D, 0x89, 0xCE, 0x33, 0x34};

// --- Proměnné pro gyroskop ---
float pitchFiltered = 0;
float rollFiltered = 0;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 300;
char lastState = 'X';

// --- ADC / měření baterie ---
const int BAT_PIN = 34;
const float R1 = 216000.0;
const float R2 = 100000.0;
const float ADC_REF = 3.3;
const int ADC_RES = 4095;

float batteryVoltage = 0.0;
int batteryPercent = 0;
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 10000;

const float kalibracniFaktor = 1.097;
const float posun = 0.0;

// --- Funkce pro přepínání OLED stránek ---
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

// --- Funkce pro měření a aktualizaci baterie ---
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
 // Kontrola I2C pro Gyroskop
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 nenalezen!");
    while (1);
  }
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
  Serial.println("MPU6050 OK");
  // Kontrola I2C pro OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED nenalezen!");
    while (1);
  }
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
  handleButton(); // Tlačítko pro přepínání stránek

  // Kontrola stavu baterie
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

  char state = 'I'; // Sa
  if (pitchFiltered < -10 && pitchFiltered > -50) state = 'F';
  else if (pitchFiltered > 10 && pitchFiltered < 50) state = 'B';
  else if (rollFiltered < -10 && rollFiltered > -50) state = 'L';
  else if (rollFiltered > 10 && rollFiltered < 50) state = 'R';

  if (state != lastState || millis() - lastSend >= SEND_INTERVAL) {
    myMessage.state = state;
    esp_now_send(receiverAddress, (uint8_t*)&myMessage, sizeof(myMessage));
    Serial.print("Odeslán stav: ");
    Serial.println(state);
    lastState = state;
    lastSend = millis();
  }

  display.clearDisplay();

  // === OBSAH STRÁNEK OLED ===
  if (currentScreen == 0) { // === Tečka v kruhu ===
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2;
    int radius = 30; // Radius kruhu

    // převod roll/pitch na posun
    float scale = 1.5;
    float offsetX = rollFiltered * scale;
    float offsetY = pitchFiltered * scale;

    // výpočet vzdálenosti od středu
    float distance = sqrt(offsetX * offsetX + offsetY * offsetY);

    // omezení tečky, aby nevjela mimo kruh
    if (distance > radius - 3) {
      offsetX = offsetX * (radius - 3) / distance;
      offsetY = offsetY * (radius - 3) / distance;
    }

    int dotX = centerX + (int)offsetX;
    int dotY = centerY + (int)offsetY;

    display.drawCircle(centerX, centerY, radius, SSD1306_WHITE);
    display.fillCircle(dotX, dotY, 3, SSD1306_WHITE);

  } else if (currentScreen == 1) { // === Pitch/Roll hodnoty ===
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("== SENZOR DATA ==");
    display.print("Pitch: "); display.println(pitchFiltered, 1);
    display.print("Roll : "); display.println(rollFiltered, 1);
    display.print("Stav : "); display.write(state);
    display.print(" Baterie: "); display.print(batteryVoltage, 2);
    display.print(" V ("); display.print(batteryPercent); display.println("%)");

  } else if (currentScreen == 2) { // === Info ===
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("== INFO ==");
    display.println("Autor: Dominik Klein");
    display.println("Verze: 1.5");
    display.println("Maturitni projekt");
    display.println("SPSUL 2026");
  }

  display.display();
  delay(20);
}
