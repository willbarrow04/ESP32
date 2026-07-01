// include/pins.h  — SINGLE SOURCE OF TRUTH, match your pushed config
#pragma once

// Status LEDs
#define LED_1   40
#define LED_2   41
#define LED_3   42

// PTT buttons (active-low, internal pull-ups)
#define BTN_1    1
#define BTN_2    2

// microSD (SPI) — CONFIRM which of 10-13 is which against your wiring
#define SD_CS   10
#define SD_MOSI 11
#define SD_SCK  12
#define SD_MISO 13