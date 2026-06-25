#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "secrets.h"

static const char CA_BUNDLE[] = DIGICERT_CA_BUNDLE;

static const int MAX_RETRIES     = 5;
static const int BASE_BACKOFF_MS = 1000;
static const int MAX_BACKOFF_MS  = 30000;

// ── WiFi ──────────────────────────────────────────────────────────────────────

void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── NTP ───────────────────────────────────────────────────────────────────────

void syncNTP() {
  Serial.println("Syncing clock via NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm t;
  // A freshly booted ESP32 thinks it's 1970 — every cert looks "not yet valid".
  // Block until the clock is past 2024 to guarantee valid TLS date checks.
  while (!getLocalTime(&t) || t.tm_year + 1900 < 2024) {
    Serial.println("  waiting for NTP...");
    delay(1000);
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  Serial.printf("Clock synced: %s UTC\n", buf);
}

// ── HTTP ──────────────────────────────────────────────────────────────────────

// Returns the HTTP status code (>0) or a negative transport/TLS error code.
// 'body' is populated on any HTTP response; empty on transport error.
int getRequest(const char* url, String& body) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);      // real validation — setInsecure() is never used

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    body = "";
    return -1;
  }
  http.addHeader("Authorization", "Bearer " BACKEND_API_KEY);
  http.addHeader("Accept", "application/json");

  int status = http.GET();
  body = (status > 0) ? http.getString() : "";
  http.end();
  return status;
}

void fetchModels() {
  String url = String(BACKEND_BASE_URL) + "/v1/models";
  Serial.printf("GET %s\n", url.c_str());

  int backoffMs = BASE_BACKOFF_MS;
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    if (attempt > 1) Serial.printf("[Retry %d/%d]\n", attempt, MAX_RETRIES);

    String body;
    int status = getRequest(url.c_str(), body);

    if (status == 200) {
      Serial.println("200 OK — model list:");
      Serial.println(body);
      return;
    }

    // Auth/client errors are not retry-safe; print the body so the cause is visible.
    if (status == 401 || status == 403 || status == 404) {
      Serial.printf("Non-retryable %d — aborting.\nResponse body: %s\n",
                    status, body.c_str());
      return;
    }

    // 502 or negative (transport/TLS error) — retry with exponential backoff.
    if (attempt < MAX_RETRIES) {
      Serial.printf("Status %d — retrying in %d ms\n", status, backoffMs);
      delay(backoffMs);
      backoffMs = (backoffMs * 2 < MAX_BACKOFF_MS) ? backoffMs * 2 : MAX_BACKOFF_MS;
    } else {
      Serial.printf("Status %d — max retries reached.\nLast body: %s\n",
                    status, body.c_str());
    }
  }
}

// ── Entry points ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  connectWiFi();
  syncNTP();
  fetchModels();
}

void loop() {}
