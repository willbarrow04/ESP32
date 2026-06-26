#include "BlueClient.h"

// ─── sessionStateStr ──────────────────────────────────────────────────────────

const char* sessionStateStr(SessionState s) {
    switch (s) {
        case SessionState::IDLE:      return "IDLE";
        case SessionState::RECORDING: return "RECORDING";
        case SessionState::UPLOADING: return "UPLOADING";
        case SessionState::COMPLETE:  return "COMPLETE";
        case SessionState::FAILED:    return "FAILED";
        default:                      return "?";
    }
}

// ─── Constructor ──────────────────────────────────────────────────────────────

BlueClient::BlueClient(const char* caCert, const char* apiKey, const char* baseUrl)
    : _caCert(caCert), _apiKey(apiKey), _baseUrl(baseUrl),
      _port(443), _state(SessionState::IDLE)
{
    memset(_recordingId, 0, sizeof(_recordingId));

    // Extract host (and optional port) from base URL for http.begin(client, host, port, …).
    String url = baseUrl;
    if (url.startsWith("https://")) {
        _port = 443;
        url   = url.substring(8);
    } else if (url.startsWith("http://")) {
        _port = 80;
        url   = url.substring(7);
    }
    int colonIdx = url.indexOf(':');
    if (colonIdx >= 0) {
        _port = (uint16_t)url.substring(colonIdx + 1).toInt();
        _host = url.substring(0, colonIdx);
    } else {
        _host = url;
    }

    // Set CA bundle once; _tls is reused across all requests in this session.
    _tls.setCACert(_caCert);
}

// ─── Public: resetState ───────────────────────────────────────────────────────

void BlueClient::resetState() {
    _state = SessionState::IDLE;
    memset(_recordingId, 0, sizeof(_recordingId));
}

// ─── Public: isoNow ──────────────────────────────────────────────────────────

String BlueClient::isoNow() const {
    struct tm t;
    if (!getLocalTime(&t)) return "1970-01-01T00:00:00Z";
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return String(buf);
}

// ─── Public: createRecording ──────────────────────────────────────────────────
// POST /v1/recordings?agencyId=<id>&officerId=<id>
// 201 → { "ok": true, "data": { "recordingId": "<uuid>" } }
// State: IDLE → RECORDING

bool BlueClient::createRecording(const char* agencyId, const char* officerId,
                                  const char* startedAtISO) {
    if (_state != SessionState::IDLE) {
        Serial.printf("[BlueClient] createRecording: must be IDLE (currently %s)\n",
                      sessionStateStr(_state));
        return false;
    }

    String path = String("/v1/recordings?agencyId=") + agencyId
                + "&officerId=" + officerId;

    StaticJsonDocument<128> req;
    req["startedAt"] = startedAtISO;
    String body;
    serializeJson(req, body);

    int backoffMs = BASE_BACKOFF_MS;
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1)
            Serial.printf("[BlueClient] createRecording retry %d/%d\n", attempt, MAX_RETRIES);

        String resp;
        int status = doRequest("POST", path, "application/json", body, resp);

        if (status == 201) {
            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, resp) != DeserializationError::Ok) {
                Serial.println("[BlueClient] createRecording: JSON parse error");
                return false;
            }
            const char* id = doc["data"]["recordingId"] | "";
            if (!id || !*id) {
                Serial.println("[BlueClient] createRecording: missing recordingId in response");
                return false;
            }
            strlcpy(_recordingId, id, sizeof(_recordingId));
            _state = SessionState::RECORDING;
            Serial.printf("[BlueClient] Recording created: %s\n", _recordingId);
            return true;
        }

        if (!isRetryable(status))
            return handleHttpError(status, resp, "createRecording");

        if (attempt < MAX_RETRIES) {
            Serial.printf("[BlueClient] createRecording: HTTP %d, retrying in %d ms\n",
                          status, backoffMs);
            delay(backoffMs);
            backoffMs = min(backoffMs * 2, MAX_BACKOFF_MS);
        }
    }

    Serial.println("[BlueClient] createRecording: max retries reached");
    _state = SessionState::FAILED;
    return false;
}

// ─── Public: updateRecording ──────────────────────────────────────────────────
// PATCH /v1/recordings/{id}?agencyId=<id>
// 200 → full recording record (not parsed; logged on error)
// State: RECORDING → UPLOADING

bool BlueClient::updateRecording(const char* agencyId,
                                  const char* status,
                                  const char* endedAtISO,
                                  int         durationSeconds) {
    if (_state != SessionState::RECORDING) {
        Serial.printf("[BlueClient] updateRecording: must be RECORDING (currently %s)\n",
                      sessionStateStr(_state));
        return false;
    }

    String path = String("/v1/recordings/") + _recordingId
                + "?agencyId=" + agencyId;

    StaticJsonDocument<256> req;
    if (status)             req["status"]          = status;
    if (endedAtISO)         req["endedAt"]         = endedAtISO;
    if (durationSeconds > 0) req["durationSeconds"] = durationSeconds;
    String body;
    serializeJson(req, body);

    int backoffMs = BASE_BACKOFF_MS;
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1)
            Serial.printf("[BlueClient] updateRecording retry %d/%d\n", attempt, MAX_RETRIES);

        String resp;
        int httpStatus = doRequest("PATCH", path, "application/json", body, resp);

        if (httpStatus == 200) {
            _state = SessionState::UPLOADING;
            Serial.printf("[BlueClient] Recording updated → UPLOADING (%s)\n", _recordingId);
            return true;
        }

        if (!isRetryable(httpStatus))
            return handleHttpError(httpStatus, resp, "updateRecording");

        if (attempt < MAX_RETRIES) {
            Serial.printf("[BlueClient] updateRecording: HTTP %d, retrying in %d ms\n",
                          httpStatus, backoffMs);
            delay(backoffMs);
            backoffMs = min(backoffMs * 2, MAX_BACKOFF_MS);
        }
    }

    Serial.println("[BlueClient] updateRecording: max retries reached");
    _state = SessionState::FAILED;
    return false;
}

// ─── Public: uploadAudio ─────────────────────────────────────────────────────
// POST /v1/recordings/{id}/audio?agencyId=<id>
// Content-Type: application/octet-stream; Content-Length = audioFile.size().
// fs::File inherits from Stream — HTTPClient reads it in MTU-sized pages,
// no chunked TE, no full-file RAM allocation.
// On each retry the file is rewound to position 0 before sending.
// 200 → { "ok": true, "data": { "audioUrl": "..." } }
// State: UPLOADING → COMPLETE | FAILED

bool BlueClient::uploadAudio(const char* agencyId,
                              fs::File& audioFile,
                              String& audioUrlOut) {
    if (_state != SessionState::UPLOADING) {
        Serial.printf("[BlueClient] uploadAudio: must be UPLOADING (currently %s)\n",
                      sessionStateStr(_state));
        return false;
    }
    if (!audioFile) {
        Serial.println("[BlueClient] uploadAudio: audioFile is not open");
        _state = SessionState::FAILED;
        return false;
    }

    size_t fileSize = audioFile.size();
    String path = String("/v1/recordings/") + _recordingId
                + "/audio?agencyId=" + agencyId;

    int backoffMs = BASE_BACKOFF_MS;
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1) {
            Serial.printf("[BlueClient] uploadAudio retry %d/%d\n", attempt, MAX_RETRIES);
            audioFile.seek(0);  // rewind SD file for retry
        }

        Serial.printf("[BlueClient] Uploading %u bytes of audio\n", (unsigned)fileSize);

        String resp;
        int status = doStreamPost(path, audioFile, fileSize, resp);

        if (status == 200) {
            StaticJsonDocument<512> doc;
            if (deserializeJson(doc, resp) == DeserializationError::Ok)
                audioUrlOut = doc["data"]["audioUrl"] | "";
            _state = SessionState::COMPLETE;
            Serial.printf("[BlueClient] Audio uploaded. URL: %s\n", audioUrlOut.c_str());
            return true;
        }

        if (!isRetryable(status)) {
            _state = SessionState::FAILED;
            return handleHttpError(status, resp, "uploadAudio");
        }

        if (attempt < MAX_RETRIES) {
            Serial.printf("[BlueClient] uploadAudio: HTTP %d, retrying in %d ms\n",
                          status, backoffMs);
            delay(backoffMs);
            backoffMs = min(backoffMs * 2, MAX_BACKOFF_MS);
        }
    }

    Serial.println("[BlueClient] uploadAudio: max retries reached");
    _state = SessionState::FAILED;
    return false;
}

// ─── Public: uploadTranscript ─────────────────────────────────────────────────
// STUB — on-device STT is not viable on the ESP32-S3.
// TODO: Remove or route via a companion service / cloud post-processing step.

bool BlueClient::uploadTranscript(const char* agencyId, const char* transcriptText) {
    (void)agencyId;
    (void)transcriptText;
    Serial.println("[BlueClient] uploadTranscript: not implemented on this device (stub)");
    return false;
}

// ─── Private: doRequest ───────────────────────────────────────────────────────
// Generic helper for GET, POST, and PATCH with a String body.
// Uses the shared _tls socket; if the previous TCP connection is still open
// (server sent Connection: keep-alive) HTTPClient will reuse it.

int BlueClient::doRequest(const char* method, const String& path,
                           const char* contentType, const String& body,
                           String& respOut) {
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(true);

    if (!http.begin(_tls, _host, _port, path, (_port == 443))) {
        Serial.printf("[BlueClient] http.begin failed: %s\n", path.c_str());
        return -1;
    }

    http.addHeader("Authorization", String("Bearer ") + _apiKey);
    http.addHeader("Accept", "application/json");
    if (contentType && *contentType)
        http.addHeader("Content-Type", contentType);

    int status;
    if (strcmp(method, "GET") == 0) {
        status = http.GET();
    } else if (strcmp(method, "POST") == 0) {
        status = http.sendRequest("POST", body);
    } else if (strcmp(method, "PATCH") == 0) {
        status = http.sendRequest("PATCH", body);
    } else {
        Serial.printf("[BlueClient] doRequest: unknown method %s\n", method);
        http.end();
        return -1;
    }

    respOut = (status > 0) ? http.getString() : "";
    http.end();
    return status;
}

// ─── Private: doStreamPost ───────────────────────────────────────────────────
// Audio-specific POST.  HTTPClient::sendRequest(type, Stream*, size) sets
// Content-Length and reads from the stream in MTU-sized (~1460 B) pages — no
// chunked transfer encoding, no full-file heap allocation.
// stream must be positioned at byte 0 before each call.

int BlueClient::doStreamPost(const String& path,
                              Stream& stream, size_t streamLen,
                              String& respOut) {
    HTTPClient http;
    http.setTimeout(60000);   // large WAV files may take time over TLS
    http.setReuse(true);

    if (!http.begin(_tls, _host, _port, path, (_port == 443))) {
        Serial.println("[BlueClient] doStreamPost: http.begin failed");
        return -1;
    }

    http.addHeader("Authorization", String("Bearer ") + _apiKey);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Accept", "application/json");

    int status = http.sendRequest("POST", &stream, streamLen);

    respOut = (status > 0) ? http.getString() : "";
    http.end();
    return status;
}

// ─── Private: handleHttpError ────────────────────────────────────────────────

bool BlueClient::handleHttpError(int status, const String& body, const char* ctx) {
    Serial.printf("[BlueClient] %s: HTTP %d\n", ctx, status);
    if (body.length() > 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok && !doc["ok"].as<bool>()) {
            const char* code = doc["error"]["code"]    | "?";
            const char* msg  = doc["error"]["message"] | "?";
            Serial.printf("[BlueClient] %s: code=%s message=%s\n", ctx, code, msg);
        } else {
            Serial.printf("[BlueClient] %s: body=%s\n", ctx, body.c_str());
        }
    }
    return false;
}

// ─── Private: isRetryable ────────────────────────────────────────────────────

bool BlueClient::isRetryable(int status) const {
    // 502 = backend gateway error; negative = transport / TLS failure
    return status == 502 || status < 0;
}
