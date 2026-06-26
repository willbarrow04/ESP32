#pragma once
#include <Arduino.h>
#include <SD.h>
#include "AudioConfig.h"

// ─── AudioCapture ─────────────────────────────────────────────────────────────
// Records I2S audio from an INMP441 mic, encodes it as a WAV file on SD card,
// then provides the closed file for upload via BlueClient::uploadAudio().
//
// WAV header strategy: reserve 44 bytes of zeros at file open, stream PCM
// samples after that, then seek back on stopRecording() to patch the RIFF-size
// and data-chunk-size fields with the final byte counts.  This avoids a
// two-pass write while keeping the file a valid WAV at close time.
//
// Call order per recording session:
//   begin()  — once in setup()
//   startRecording(path) → processAudio() [loop] → stopRecording()
//   openForUpload()      — pass the returned File& to BlueClient::uploadAudio()
//   caller closes the File after upload

class AudioCapture {
public:
    AudioCapture();

    // Installs the I2S driver and leaves it stopped (no capture yet).
    // Must be called after SD.begin(). Returns false on I2S failure.
    bool begin();

    // Opens a new WAV file at 'path', writes a zeroed 44-byte placeholder
    // header, then starts I2S capture.  Returns false if the file can't be
    // created or I2S fails to start.
    bool startRecording(const char* path);

    // Reads one DMA buffer of raw I2S samples, converts to 16-bit PCM, and
    // appends to the WAV file.  Call from loop() or a FreeRTOS audio task —
    // must run faster than (I2S_DMA_BUF_CNT × I2S_DMA_BUF_LEN / SAMPLE_RATE)
    // ≈ 256 ms to avoid DMA overflow.
    // Returns false only on SD write error (sets isRecording() to false).
    // No-op (returns true) when not recording.
    //
    // TODO(freertos): Move this into a dedicated FreeRTOS task pinned to core 0
    // once the PTT state machine is wired into the main loop.
    bool processAudio();

    // Stops I2S capture, flushes pending audio, patches the WAV header sizes,
    // and closes the file.  Returns false on failure.
    // After success: isRecording() == false, openForUpload() is valid.
    bool stopRecording();

    // Re-opens the last recorded file at position 0, ready to pass directly to
    // BlueClient::uploadAudio().  Caller is responsible for closing it after upload.
    // Only valid after a successful stopRecording().
    fs::File openForUpload();

    bool     isRecording()     const { return _recording; }
    uint32_t samplesWritten()  const { return _samplesWritten; }
    uint32_t durationSeconds() const { return _samplesWritten / AUDIO_SAMPLE_RATE; }
    size_t   fileSize()        const { return _fileSize; }

private:
    bool     _i2sReady;
    bool     _recording;
    fs::File _file;
    char     _filePath[64];
    uint32_t _samplesWritten;
    size_t   _fileSize;

    bool initI2S();

    // Writes a complete 44-byte WAV header with the given PCM data byte count.
    // Call with dataBytes=0 as the placeholder at recording start.
    void writeWavHeader(uint32_t dataBytes);

    // Seeks to the two size fields in the open file and patches them with the
    // correct values derived from _samplesWritten.  File position is undefined
    // after this call; close immediately.
    void patchWavSizes();
};
