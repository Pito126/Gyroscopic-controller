#include <esp_now.h>
#include <WiFi.h>

// --- Struktura zprávy ---
typedef struct struct_message {
  char state[3];   // např. "F1", "B3", "R4"
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

// --- Směr zapojení motorů ---
// Pokud se motor točí obráceně, změň hodnotu na "true"
#define REVERSE_A false
#define REVERSE_B true   // <- pravděpodobně bude potřeba prohodit, proto nechávám true

// --- Proměnné ---
unsigned long lastRecv = 0;
const unsigned long TIMEOUT_MS = 1000;
int speedLevels[4] = {100, 150, 200, 255}; // úrovně rychlosti

// --- Funkce pro řízení motorů ---
void stopMotors() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
  analogWrite(PWM_A, 0);
  analogWrite(PWM_B, 0);
}

void setMotorA(bool forward, int pwm) {
  if (REVERSE_A) forward = !forward;
  digitalWrite(AIN1, forward ? HIGH : LOW);
  digitalWrite(AIN2, forward ? LOW : HIGH);
  analogWrite(PWM_A, pwm);
}

void setMotorB(bool forward, int pwm) {
  if (REVERSE_B) forward = !forward;
  digitalWrite(BIN1, forward ? HIGH : LOW);
  digitalWrite(BIN2, forward ? LOW : HIGH);
  analogWrite(PWM_B, pwm);
}

// --- Pohybové funkce ---
void forward(int lvl) {
  int pwm = speedLevels[lvl];
  setMotorA(true, pwm);
  setMotorB(true, pwm);
}

void backward(int lvl) {
  int pwm = speedLevels[lvl];
  setMotorA(false, pwm);
  setMotorB(false, pwm);
}

void left(int lvl) {
  int pwm = speedLevels[lvl];
  setMotorA(false, pwm);
  setMotorB(true, pwm);
}

void right(int lvl) {
  int pwm = speedLevels[lvl];
  setMotorA(true, pwm);
  setMotorB(false, pwm);
}

// --- ESP-NOW callback ---
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&myMessage, incomingData, sizeof(myMessage));
  lastRecv = millis();

  Serial.print("Pitch: "); Serial.print(myMessage.pitch, 1);
  Serial.print("  Roll: "); Serial.print(myMessage.roll, 1);
  Serial.print("  Stav: "); Serial.println(myMessage.state);

  char dir = myMessage.state[0]; // F, B, L, R
  int level = myMessage.state[1] - '1'; // '1'–'4' → 0–3

  if (level < 0 || level > 3) {
    stopMotors();
    return;
  }

  switch (dir) {
    case 'F': forward(level); break;
    case 'B': backward(level); break;
    case 'L': left(level); break;
    case 'R': right(level); break;
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
