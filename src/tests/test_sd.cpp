#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);   // begin(SCK, MISO, MOSI, CS)

  if (!SD.begin(SD_CS, SPI, 4000000)) {          // 4 MHz, conservative for jumpers
    Serial.println("SD init FAILED");
    return;
  }
  Serial.println("SD init OK");

  File f = SD.open("/bringup.txt", FILE_WRITE);
  if (!f) { Serial.println("open-for-write FAILED"); return; }
  f.println("blue partner sd bringup ok");
  f.close();
  Serial.println("wrote /bringup.txt");

  f = SD.open("/bringup.txt", FILE_READ);
  if (!f) { Serial.println("open-for-read FAILED"); return; }
  Serial.print("read back: ");
  while (f.available()) Serial.write(f.read());
  f.close();

  Serial.println("root listing:");
  File root = SD.open("/");
  for (File e = root.openNextFile(); e; e = root.openNextFile()) {
    Serial.printf("  %s  %u bytes\n", e.name(), (unsigned)e.size());
    e.close();
  }
}

void loop() {}