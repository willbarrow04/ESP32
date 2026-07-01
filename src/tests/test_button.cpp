#include <Arduino.h>
#include "pins.h"

const int btns[] = { BTN_1, BTN_2 };
const int N = sizeof(btns) / sizeof(btns[0]);

int           stableState[N];
int           lastReading[N];
unsigned long lastChange[N];
const unsigned long DEBOUNCE_MS = 30;

void setup() {
  Serial.begin(115200);
  delay(1000);
  for (int i = 0; i < N; i++) {
    pinMode(btns[i], INPUT_PULLUP);
    stableState[i] = HIGH;      // idle = HIGH because pull-up
    lastReading[i] = HIGH;
    lastChange[i]  = 0;
  }
  Serial.println("press buttons...");
}

void loop() {
  for (int i = 0; i < N; i++) {
    int reading = digitalRead(btns[i]);
    if (reading != lastReading[i]) {
      lastChange[i] = millis();
      lastReading[i] = reading;
    }
    if (millis() - lastChange[i] > DEBOUNCE_MS) {
      if (reading != stableState[i]) {
        stableState[i] = reading;
        Serial.printf("BTN GPIO %d -> %s\n",
                      btns[i], reading == LOW ? "PRESSED" : "released");
      }
    }
  }
}