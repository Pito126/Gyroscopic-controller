#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA=21, SCL=22 (standard pro ESP32)

  Serial.println("\nI2C Scanner");
}

void loop() {
  byte error, address;
  int nDevices;

  Serial.println("Vyhledavam I2C zarizeni...");
  nDevices = 0;

  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C zarizeni nalezeno na adrese 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");

      nDevices++;
    }
    else if (error == 4) {
      Serial.print("Neznamy error na adrese 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }    
  }

  if (nDevices == 0)
    Serial.println("Zadna I2C zarizeni nenalezena\n");
  else
    Serial.println("Hotovo\n");

  delay(2000); // čekej 2 sekundy a skenuj znovu
}
