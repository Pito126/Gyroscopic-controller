#include <esp_now.h>
#include <WiFi.h>

// --- Struktura zprávy ---
typedef struct struct_message {
  char state;
  float pitch;
  float roll;
} struct_message;

struct_message myMessage;

// --- TB6612 piny ---
#define AIN1 17
#define AIN2 16
#define BIN1 18
#define BIN2 19
#define STBY 21
#define PWM_A 4
#define PWM_B 5

// --- Proměnné ---
unsigned long lastRecv = 0;
const unsigned long TIMEOUT_MS = 1000;
int pwm_a = 255; // Výkon motoru A (0–255)
int pwm_b = 255; // Výkon motoru B (0–255)

// --- Funkce motorů ---
void stopMotors() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
  analogWrite(PWM_A, 0);
  analogWrite(PWM_B, 0);
}

void forward() {
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWM_A, pwm_a);
  analogWrite(PWM_B, pwm_b);
}

void backward() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);
  analogWrite(PWM_A, pwm_a);
  analogWrite(PWM_B, pwm_b);
}

void left() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWM_A, pwm_a);
  analogWrite(PWM_B, pwm_b);
}

void right() {
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);
  analogWrite(PWM_A, pwm_a);
  analogWrite(PWM_B, pwm_b);
}

// --- ESP-NOW callback ---
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&myMessage, incomingData, sizeof(myMessage));
  lastRecv = millis();

  Serial.print("Pitch: "); Serial.print(myMessage.pitch, 1);
  Serial.print("  Roll: "); Serial.print(myMessage.roll, 1);
  Serial.print("  Stav: "); Serial.println(myMessage.state);

  switch (myMessage.state) {
    case 'F': forward(); break;
    case 'B': backward(); break;
    case 'L': left(); break;
    case 'R': right(); break;
    default: stopMotors(); break;
  }
}

// --- Nastavení ---
void setup() {
  Serial.begin(115200);

  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  pinMode(PWM_A, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  digitalWrite(STBY, HIGH);
  stopMotors();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba inicializace ESP-NOW!");
    while (1);
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Receiver pripraven");
}

// --- Hlavní smyčka ---
void loop() {
  if (millis() - lastRecv > TIMEOUT_MS) {
    stopMotors();
  }
}
