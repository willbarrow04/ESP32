#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

static const char CA_BUNDLE[] = DIGICERT_CA_BUNDLE;

static const int MAX_RETRIES     = 5;
static const int BASE_BACKOFF_MS = 1000;
static const int MAX_BACKOFF_MS  = 30000;

// ── Token cache ───────────────────────────────────────────────────────────────
static char          s_accessToken[2048];
static unsigned long s_tokenExpireMs = 0;

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
  while (!getLocalTime(&t) || t.tm_year + 1900 < 2024) {
    Serial.println("  waiting for NTP...");
    delay(1000);
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  Serial.printf("Clock synced: %s UTC\n", buf);
}

// ── Token acquisition (OAuth 2.0 client credentials) ─────────────────────────

bool fetchToken() {
  Serial.println("[Token] Requesting access token...");

  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  HTTPClient http;
  http.setTimeout(15000);

  String url = "https://login.microsoftonline.us/"
               ENTRA_TENANT_ID
               "/oauth2/v2.0/token";

  if (!http.begin(client, url)) {
    Serial.println("[Token] http.begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=client_credentials"
                "&client_id="     ENTRA_CLIENT_ID
                "&client_secret=" ENTRA_CLIENT_SECRET
                "&scope="         ENTRA_SCOPE;

  int status = http.POST(body);
  String resp = (status > 0) ? http.getString() : "";
  http.end();

  if (status != 200) {
    Serial.printf("[Token] HTTP %d: %s\n", status, resp.c_str());
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp) != DeserializationError::Ok) {
    Serial.println("[Token] JSON parse error");
    return false;
  }

  const char* token = doc["access_token"] | "";
  int expiresIn     = doc["expires_in"]   | 3600;

  if (!token || !*token) {
    Serial.println("[Token] No access_token in response");
    return false;
  }

  strlcpy(s_accessToken, token, sizeof(s_accessToken));
  s_tokenExpireMs = millis() + (unsigned long)(expiresIn - 60) * 1000UL;

  Serial.printf("[Token] OK — expires in %ds\n", expiresIn);
  return true;
}

bool ensureToken() {
  if (s_accessToken[0] != '\0' && millis() < s_tokenExpireMs) return true;
  return fetchToken();
}

// ── HTTP GET ──────────────────────────────────────────────────────────────────

int getRequest(const char* url, String& body) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    body = "";
    return -1;
  }
  http.addHeader("Authorization", String("Bearer ") + s_accessToken);
  http.addHeader("Accept", "application/json");

  int status = http.GET();
  body = (status > 0) ? http.getString() : "";
  http.end();
  return status;
}

// ── fetchModels ───────────────────────────────────────────────────────────────

void fetchModels() {
  if (!ensureToken()) {
    Serial.println("[fetchModels] Could not acquire token — aborting");
    return;
  }

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

    if (status == 401 || status == 403 || status == 404) {
      Serial.printf("Non-retryable %d — aborting.\nResponse body: %s\n",
                    status, body.c_str());
      return;
    }

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
  delay(1000);

  connectWiFi();
  syncNTP();
  fetchModels();
}

void loop() {}
