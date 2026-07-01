#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(2000);              // let USB CDC enumerate
  Serial.println("console alive");
}

void loop() {
  Serial.println("tick");
  delay(1000);
}