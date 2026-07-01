#include <Arduino.h>
#include "pins.h"

const int leds[] = { LED_1, LED_2, LED_3 };
const int N = sizeof(leds) / sizeof(leds[0]);

void setup() {
  Serial.begin(115200);
  delay(1000);
  for (int i = 0; i < N; i++) pinMode(leds[i], OUTPUT);
}

void loop() {
  for (int i = 0; i < N; i++) {
    Serial.printf("LED on GPIO %d\n", leds[i]);
    digitalWrite(leds[i], HIGH);
    delay(600);
    digitalWrite(leds[i], LOW);
    delay(200);
  }
}