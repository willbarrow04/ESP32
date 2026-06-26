#pragma once
#include <Arduino.h>
#include <FS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// TODO(provisioning): agencyId and officerId must be written to NVS flash
// during device enrollment/pairing. On boot, read them from NVS and pass to
// every BlueClient method. Currently the caller supplies them directly.

// ─── Session state machine ────────────────────────────────────────────────────

enum class SessionState : uint8_t {
    IDLE,        // no active recording
    RECORDING,   // createRecording succeeded; audio capture in progress
    UPLOADING,   // PATCH sent (status=uploading); audio being streamed
    COMPLETE,    // uploadAudio returned 200; session done
    FAILED       // unrecoverable error; store-and-forward retry on next boot
};

const char* sessionStateStr(SessionState s);

// ─── BlueClient ───────────────────────────────────────────────────────────────
// One instance per device lifetime.  Holds a single TLS socket (_tls) so that
// sequential API calls within a recording session can reuse the TCP connection
// if the server supports keep-alive (reduces per-call TLS handshake overhead).

class BlueClient {
public:
    static constexpr int    MAX_RETRIES     = 5;
    static constexpr int    BASE_BACKOFF_MS = 1000;
    static constexpr int    MAX_BACKOFF_MS  = 30000;
    // caCert must remain valid for the lifetime of this object (point at a
    // static/global buffer — e.g. the CA_BUNDLE array in main.cpp).
    BlueClient(const char* caCert, const char* apiKey, const char* baseUrl);

    // ── Endpoint methods ───────────────────────────────────────────────────

    // POST /v1/recordings?agencyId=&officerId=
    // Body: { "startedAt": "<ISO8601>" }
    // 201 → stores recordingId(); state: IDLE → RECORDING
    bool createRecording(const char* agencyId, const char* officerId,
                         const char* startedAtISO);

    // PATCH /v1/recordings/{id}?agencyId=
    // Body: any combination of status / endedAt / durationSeconds (omit by
    // passing nullptr / 0).  Typical call at record-stop:
    //   status="uploading", endedAt=isoNow(), durationSeconds=elapsed
    // 200 → state: RECORDING → UPLOADING
    bool updateRecording(const char* agencyId,
                         const char* status,         // nullable → field omitted
                         const char* endedAtISO,     // nullable → field omitted
                         int         durationSeconds  // 0 → field omitted
                         );

    // POST /v1/recordings/{id}/audio?agencyId=
    // Content-Type: application/octet-stream; Content-Length = audioFile.size().
    // audioFile must be open at position 0 (use AudioCapture::openForUpload()).
    // Streams through TLS in MTU-sized pages — file is never fully in RAM.
    // On retry the file is rewound to 0 automatically.
    // 200 → audioUrlOut populated; state: UPLOADING → COMPLETE
    //       permanent error → state: FAILED (caller may retry from SD)
    bool uploadAudio(const char* agencyId,
                     fs::File& audioFile,
                     String& audioUrlOut);

    // POST /v1/recordings/{id}/transcript?agencyId=  ── STUB ──
    // TODO: On-device STT is not viable on the ESP32-S3.  Remove or route
    //   through a companion service / cloud post-processing step.
    bool uploadTranscript(const char* agencyId, const char* transcriptText);

    // ── Accessors ──────────────────────────────────────────────────────────
    SessionState state()       const { return _state; }
    const char*  recordingId() const { return _recordingId; }

    // Returns an ISO-8601 UTC timestamp from the system clock.
    // NTP must have been synced before calling (enforced by syncNTP in main).
    String isoNow() const;

    // Reset to IDLE and clear recordingId.
    // Call after COMPLETE, or after a FAILED session has been persisted to SD.
    void resetState();

private:
    const char*      _caCert;
    const char*      _apiKey;
    const char*      _baseUrl;
    String           _host;
    uint16_t         _port;
    char             _recordingId[64];
    SessionState     _state;
    WiFiClientSecure _tls;   // shared TLS socket — keep-alive across calls

    // Generic HTTP helper (GET / POST / PATCH with a String body).
    int doRequest(const char* method, const String& path,
                  const char* contentType, const String& body,
                  String& respOut);

    // Audio-specific POST: passes an open Stream& directly to HTTPClient so
    // data is read in MTU-sized pages without a full-file allocation.
    int doStreamPost(const String& path,
                     Stream& stream, size_t streamLen,
                     String& respOut);

    // Prints HTTP status + parses and logs the recordings error envelope.
    // Always returns false so callers can `return handleHttpError(...)`.
    bool handleHttpError(int status, const String& body, const char* ctx);

    bool isRetryable(int status) const;
};
