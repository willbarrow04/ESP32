#pragma once

// ── Encode format ─────────────────────────────────────────────────────────────
// TODO(audio-format): Confirm with Austin/Madmadison that 16 kHz mono PCM WAV
// is what the cloud STT pipeline expects.  Ask whether a compressed format
// (Opus, ADPCM) is preferred to cut upload bandwidth — WAV at 16 kHz mono
// runs 32 kB/s uncompressed.  Change AUDIO_FORMAT_CURRENT to switch codec
// without hunting through the codebase.

#define AUDIO_FMT_WAV 1   // 16-bit PCM in RIFF/WAV container (current)
// #define AUDIO_FMT_OPUS 2  // placeholder — requires an encoder library

#define AUDIO_FORMAT_CURRENT AUDIO_FMT_WAV

// ── PCM parameters ────────────────────────────────────────────────────────────
static constexpr uint32_t AUDIO_SAMPLE_RATE     = 16000;
static constexpr uint16_t AUDIO_CHANNELS        = 1;      // mono
static constexpr uint16_t AUDIO_BITS_PER_SAMPLE = 16;
// Derived — kept as named constants so WAV-header math is self-documenting.
static constexpr uint32_t AUDIO_BYTE_RATE   = AUDIO_SAMPLE_RATE * AUDIO_CHANNELS
                                               * (AUDIO_BITS_PER_SAMPLE / 8);  // 32000
static constexpr uint16_t AUDIO_BLOCK_ALIGN = AUDIO_CHANNELS
                                               * (AUDIO_BITS_PER_SAMPLE / 8);  // 2

// ── I2S / INMP441 ─────────────────────────────────────────────────────────────
// NOTE: Uses the legacy IDF I2S driver (driver/i2s.h).  This API is deprecated
// in IDF 5.x but still compiles via the compat layer in Arduino-ESP32 3.x.
// Migrate to driver/i2s_std.h before production if compiler warnings appear.
//
// Breadboard wiring (finalized 2026-07-01):
//
// INMP441 mic:
//   VDD → 3.3 V rail  (1 µF + 0.1 µF bypass caps to GND rail, separate BB rows)
//   GND → GND rail
//   L/R → GND rail  (selects left channel; I2S_CHANNEL_FMT_ONLY_LEFT in driver)
//   SCK → GPIO 4  (I2S_SCK_PIN — shared bus line with MAX98357A BCLK)
//   WS  → GPIO 5  (I2S_WS_PIN  — shared bus line with MAX98357A LRC)
//   SD  → GPIO 6  (I2S_SD_PIN  — mic data, input only)
//
// MAX98357A amplifier:
//   BCLK → GPIO 4  (shared with INMP441 SCK)
//   LRC  → GPIO 5  (shared with INMP441 WS)
//   DIN  → GPIO 7  (I2S_DOUT_PIN — speaker data out from ESP32)
//   GAIN → 100 kΩ to GND rail  (sets +9 dB gain; passive, no GPIO)
//   SD   → 100 kΩ to Vin rail  (keeps amp enabled; passive, no GPIO)
//   GND  → GND rail  (10 µF + 0.1 µF bypass caps to GND)
//   Vin  → 3.3 V / 5 V rail  (10 µF + 0.1 µF bypass caps to Vin)

#define I2S_MIC_PORT_NUM  0     // numeric value of I2S_NUM_0; cast where i2s_port_t needed
#define I2S_SCK_PIN       4     // I2S bit clock — INMP441 SCK + MAX98357A BCLK
#define I2S_WS_PIN        5     // I2S word select — INMP441 WS + MAX98357A LRC
#define I2S_SD_PIN        6     // INMP441 serial data (mic input)
#define I2S_DOUT_PIN      7     // MAX98357A DIN (speaker output)
#define I2S_DMA_BUF_CNT   8    // DMA buffer count
#define I2S_DMA_BUF_LEN   512  // samples per DMA buffer

// Samples read per i2s_read() call — one full DMA buffer at 32-bit width.
#define I2S_READ_SAMPLES  I2S_DMA_BUF_LEN

// ── SD card (SPI) ─────────────────────────────────────────────────────────────
// TODO(pin-config): SD_CS_PIN needs a new GPIO assignment — GPIO 5 is now
// occupied by I2S_WS_PIN (see above).  Set this to whatever chip-select line
// you wire on the breadboard before first flash.
// If your board uses non-default SPI pins (not the ESP32-S3 default 11/13/12),
// call SPI.begin(sck, miso, mosi) in setup() before SD.begin().

#define SD_CS_PIN  5    // CONFLICT: GPIO 5 is now I2S_WS_PIN — assign a free GPIO here
