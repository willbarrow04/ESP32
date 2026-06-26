#include "AudioCapture.h"
#include <driver/i2s.h>

// ─── WAV header ───────────────────────────────────────────────────────────────
// 44-byte RIFF/WAV layout, little-endian (matches ESP32 native byte order).
// Packed to prevent any compiler padding from breaking the binary layout.

struct __attribute__((packed)) WavHeader {
    uint8_t  riffId[4];     // "RIFF"
    uint32_t riffSize;      // file size − 8 (patched on close)
    uint8_t  waveId[4];     // "WAVE"
    uint8_t  fmtId[4];      // "fmt "
    uint32_t fmtSize;       // 16 for PCM
    uint16_t audioFormat;   // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;      // sampleRate × numChannels × bitsPerSample/8
    uint16_t blockAlign;    // numChannels × bitsPerSample/8
    uint16_t bitsPerSample;
    uint8_t  dataId[4];     // "data"
    uint32_t dataSize;      // PCM data bytes (patched on close)
};
static_assert(sizeof(WavHeader) == 44, "WAV header must be exactly 44 bytes");

// ─── Constructor ──────────────────────────────────────────────────────────────

AudioCapture::AudioCapture()
    : _i2sReady(false), _recording(false),
      _samplesWritten(0), _fileSize(0)
{
    _filePath[0] = '\0';
}

// ─── begin ────────────────────────────────────────────────────────────────────

bool AudioCapture::begin() {
    if (!initI2S()) return false;
    // Stop I2S after install so we don't fill DMA buffers before recording starts.
    i2s_stop((i2s_port_t)I2S_MIC_PORT_NUM);
    _i2sReady = true;
    Serial.println("[Audio] I2S ready");
    return true;
}

// ─── startRecording ───────────────────────────────────────────────────────────

bool AudioCapture::startRecording(const char* path) {
    if (!_i2sReady) {
        Serial.println("[Audio] startRecording: I2S not initialised — call begin() first");
        return false;
    }
    if (_recording) {
        Serial.println("[Audio] startRecording: already recording");
        return false;
    }

    strlcpy(_filePath, path, sizeof(_filePath));
    _file = SD.open(_filePath, FILE_WRITE);
    if (!_file) {
        Serial.printf("[Audio] Failed to open %s for write\n", _filePath);
        return false;
    }

    _samplesWritten = 0;

    // Write placeholder header (dataBytes=0); sizes patched in stopRecording().
    writeWavHeader(0);

    // Clear any stale samples from DMA buffers accumulated while stopped.
    i2s_zero_dma_buffer((i2s_port_t)I2S_MIC_PORT_NUM);
    if (i2s_start((i2s_port_t)I2S_MIC_PORT_NUM) != ESP_OK) {
        Serial.println("[Audio] i2s_start failed");
        _file.close();
        return false;
    }

    _recording = true;
    Serial.printf("[Audio] Recording started → %s\n", _filePath);
    return true;
}

// ─── processAudio ─────────────────────────────────────────────────────────────

bool AudioCapture::processAudio() {
    if (!_recording) return true;

    // Stack buffers: 512 × 4 B raw + 512 × 2 B PCM = 3 KB — safe on ESP32-S3.
    int32_t raw[I2S_READ_SAMPLES];
    size_t  bytesRead = 0;

    esp_err_t err = i2s_read((i2s_port_t)I2S_MIC_PORT_NUM,
                              raw, sizeof(raw),
                              &bytesRead, pdMS_TO_TICKS(100));

    if (err != ESP_OK || bytesRead == 0) return true;  // timeout, not a hard error

    int samplesRead = (int)(bytesRead / sizeof(int32_t));

    // INMP441 output: 24-bit audio value left-justified in a 32-bit I2S frame.
    //   bits [31:8] = sample MSB-first; bits [7:0] = always 0.
    // Right-shift 16 → keep the top 16 bits as a signed int16_t PCM sample.
    int16_t pcm[I2S_READ_SAMPLES];
    for (int i = 0; i < samplesRead; i++) {
        pcm[i] = (int16_t)(raw[i] >> 16);
    }

    size_t toWrite = (size_t)samplesRead * sizeof(int16_t);
    if (_file.write((uint8_t*)pcm, toWrite) != toWrite) {
        Serial.println("[Audio] SD write error — halting capture");
        _recording = false;
        return false;
    }

    _samplesWritten += (uint32_t)samplesRead;
    return true;
}

// ─── stopRecording ────────────────────────────────────────────────────────────

bool AudioCapture::stopRecording() {
    if (!_recording) return false;

    _recording = false;
    i2s_stop((i2s_port_t)I2S_MIC_PORT_NUM);

    // Patch the two WAV size fields now that the final sample count is known.
    patchWavSizes();

    _fileSize = _file.size();
    _file.close();

    Serial.printf("[Audio] Stopped: %u samples / %u s / %u bytes\n",
                  _samplesWritten, durationSeconds(), (unsigned)_fileSize);
    return true;
}

// ─── openForUpload ────────────────────────────────────────────────────────────

fs::File AudioCapture::openForUpload() {
    return SD.open(_filePath, FILE_READ);
}

// ─── Private: initI2S ────────────────────────────────────────────────────────

bool AudioCapture::initI2S() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = AUDIO_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;  // INMP441 outputs 32-bit frames
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;  // L/R pin tied to GND
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = I2S_DMA_BUF_CNT;
    cfg.dma_buf_len          = I2S_DMA_BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    if (i2s_driver_install((i2s_port_t)I2S_MIC_PORT_NUM, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[Audio] I2S driver install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_SCK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = I2S_SD_PIN;

    if (i2s_set_pin((i2s_port_t)I2S_MIC_PORT_NUM, &pins) != ESP_OK) {
        Serial.println("[Audio] I2S pin config failed");
        i2s_driver_uninstall((i2s_port_t)I2S_MIC_PORT_NUM);
        return false;
    }

    return true;
}

// ─── Private: writeWavHeader ──────────────────────────────────────────────────

void AudioCapture::writeWavHeader(uint32_t dataBytes) {
    WavHeader h = {};
    memcpy(h.riffId, "RIFF", 4);
    h.riffSize      = dataBytes + 36;   // 36 = "WAVE" + fmt chunk (24 B) + "data" + size field
    memcpy(h.waveId, "WAVE", 4);
    memcpy(h.fmtId,  "fmt ", 4);
    h.fmtSize       = 16;
    h.audioFormat   = 1;                // PCM
    h.numChannels   = AUDIO_CHANNELS;
    h.sampleRate    = AUDIO_SAMPLE_RATE;
    h.byteRate      = AUDIO_BYTE_RATE;
    h.blockAlign    = AUDIO_BLOCK_ALIGN;
    h.bitsPerSample = AUDIO_BITS_PER_SAMPLE;
    memcpy(h.dataId, "data", 4);
    h.dataSize      = dataBytes;
    _file.write((uint8_t*)&h, sizeof(h));
}

// ─── Private: patchWavSizes ──────────────────────────────────────────────────

void AudioCapture::patchWavSizes() {
    uint32_t dataBytes = _samplesWritten * AUDIO_BLOCK_ALIGN;
    uint32_t riffSize  = dataBytes + 36;

    // Seek back and overwrite only the two 4-byte size fields.
    _file.seek(4);
    _file.write((uint8_t*)&riffSize,  4);   // bytes 4–7: RIFF chunk size
    _file.seek(40);
    _file.write((uint8_t*)&dataBytes, 4);   // bytes 40–43: data sub-chunk size
}
