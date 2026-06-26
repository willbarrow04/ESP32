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
// TODO(pin-config): Verify these GPIO numbers against your board schematic
// before first flash.  INMP441 wiring:
//   VDD → 3.3 V,  GND → GND,  L/R → GND (selects left channel output)
//   SCK → I2S_SCK_PIN (bit clock)
//   WS  → I2S_WS_PIN  (word select / LRCK)
//   SD  → I2S_SD_PIN  (serial data out from mic)

#define I2S_MIC_PORT_NUM  0     // numeric value of I2S_NUM_0; cast where i2s_port_t needed
#define I2S_SCK_PIN       14    // TODO: confirm GPIO
#define I2S_WS_PIN        15    // TODO: confirm GPIO
#define I2S_SD_PIN        32    // TODO: confirm GPIO (INMP441 data)
#define I2S_DMA_BUF_CNT   8    // DMA buffer count
#define I2S_DMA_BUF_LEN   512  // samples per DMA buffer

// Samples read per i2s_read() call — one full DMA buffer at 32-bit width.
#define I2S_READ_SAMPLES  I2S_DMA_BUF_LEN

// ── SD card (SPI) ─────────────────────────────────────────────────────────────
// TODO(pin-config): Set SD_CS_PIN to your chip-select GPIO.
// If your board uses non-default SPI pins (not the ESP32-S3 default 11/13/12),
// call SPI.begin(sck, miso, mosi) in setup() before SD.begin().

#define SD_CS_PIN  5    // TODO: confirm GPIO
