# Hardware Bring-Up — Verified Peripherals

Bring-up done on USB power (no Pololu). Each peripheral tested in isolation
via its own env in platformio.ini, then confirmed working.

Date: 2026-07-01

## Status LEDs — VERIFIED ✓
- Blue LED → GPIO 40
- Yellow LED → GPIO 41
- Red LED → GPIO 42
- Wiring: 
    Red and Yellow LED: GPIO → resistor (220Ω) → LED anode, cathode → GND
    LED: GPIO → resistor (220Ω) → LED anode, cathode → GND
- Test: test_led — each LED lights in sequence, matches printed GPIO

## PTT Buttons — VERIFIED ✓
- BTN_1 → GPIO 1
- BTN_2 → GPIO 2
- Config: INPUT_PULLUP, active-low (idle HIGH, pressed LOW)
- Debounce: 30 ms, one clean press/release event confirmed
- Wiring: button across GPIO and GND (diagonal pins on tactile switch)
- Test: test_button

## microSD (SPI) — VERIFIED ✓
- CS   → GPIO 10
- MOSI → GPIO 11
- SCK  → GPIO 12
- MISO → GPIO 13
- Breakout: WWZMDiB 6-pin bare 3.3V (headers SOLDERED — press-fit failed)
- Card: microSDHC, FAT32
- SPI clock: 4 MHz (conservative for breadboard)
- Test: test_sd — init OK, write + read-back + root listing all pass
- NOTE: bare breakout, no onboard pull-ups. If future SD flakiness,
  first suspects: solder joints, then 10kΩ pull-ups on CS/MOSI/MISO/SCK.

## NOT yet verified (waits on Pololu)
- INMP441 mic — I2S GPIO 4/5/6
- MAX98357A amp — I2S GPIO 4/5/7 (shared bus)

## Source of truth
All pins defined in include/pins.h — this doc must match that header.