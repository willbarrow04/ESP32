#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("hello from ESP32-S3");
  Serial.printf("PSRAM detected: %d bytes\n", ESP.getPsramSize());
  Serial.printf("Flash size:     %d bytes\n", ESP.getFlashChipSize());
}

void loop() {}