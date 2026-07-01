# BP Dev Board — CLAUDE.md

## Project

Firmware for the **Blue Partner Body-Worn Capture Device** — an ESP32-S3 worn by patrol officers. Communicates with a production Azure Function App backend over HTTPS. CJIS context: TLS validation is mandatory everywhere.

Current state: Entra ID client-credentials OAuth flow wired; recordings API client (`BlueClient`) and audio capture pipeline (`AudioCapture`) implemented but not yet wired into `main.cpp`. `main.cpp` currently does WiFi → NTP → token fetch → `GET /v1/models` smoke test.

## Toolchain

- **Build system**: PlatformIO (`platformio.ini`)
- **Framework**: Arduino (Espressif Arduino core over ESP-IDF v4.4.7 + FreeRTOS)
- **Target**: `esp32-s3-devkitc-1` — Xtensa dual-core, 240 MHz *(confirm exact board ID before first flash on a clone)*
- **Flash**: 16 MB, QIO_OPI mode
- **PSRAM**: 8 MB, enabled via `-DBOARD_HAS_PSRAM`
- **Serial monitor**: 115200 baud
- **Debugger**: GDB via PlatformIO, VS Code launch config present

Build: `pio run` — flash: `pio run -t upload` — monitor: `pio device monitor`

mbedTLS debug: temporarily add `-DCORE_DEBUG_LEVEL=4` to `build_flags` to see TLS rejection detail.

## File layout

```
src/
  main.cpp              — WiFi, NTP, Entra token fetch, GET /v1/models smoke test
  BlueClient.h/.cpp     — recordings API client; session state machine
  AudioCapture.h/.cpp   — I2S capture from INMP441, WAV encoding to SD card
  AudioConfig.h         — PCM constants, I2S pin assignments, SD CS pin
  secrets.h             — real credentials; gitignored, never committed
  secrets.h.example     — placeholder template; committed
platformio.ini
.gitignore              — includes src/secrets.h
CLAUDE.md
```

## Secrets discipline

All credentials and the TLS CA bundle live in `src/secrets.h` as `#define` macros:

| Macro | Contents |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | Bench/patrol WiFi |
| `BACKEND_BASE_URL` | `https://bp-core-demo-orchestrator.azurewebsites.us` |
| `BACKEND_API_KEY` | Shared bearer key (used by `BlueClient`) |
| `ENTRA_TENANT_ID` | Azure AD tenant GUID |
| `ENTRA_CLIENT_ID` | App registration client ID for device credentials |
| `ENTRA_CLIENT_SECRET` | Client secret for the above registration |
| `ENTRA_SCOPE` | OAuth scope (e.g. `api://…/.default`) |
| `DIGICERT_CA_BUNDLE` | Root CA PEM(s) for `*.azurewebsites.us` and `login.microsoftonline.us` |

`src/secrets.h` is listed in `.gitignore`. `src/secrets.h.example` carries identical structure with placeholder values and is committed. **Note:** `secrets.h.example` does not yet include the `ENTRA_*` macros — update it when adding the first Entra-aware device.

## Backend API

- Base URL: `https://bp-core-demo-orchestrator.azurewebsites.us` (Azure Government)
- All routes prefixed `/v1`
- Auth: `Authorization: Bearer <token>` — either the Entra OAuth token (in `main.cpp`) or `BACKEND_API_KEY` (passed to `BlueClient`)

| Endpoint | Method | Notes |
|---|---|---|
| `/v1/models` | GET | Smoke-test endpoint; returns allowed model IDs |
| `/v1/recordings` | POST | Create recording; `?agencyId=&officerId=`; 201 → `{ data: { recordingId } }` |
| `/v1/recordings/{id}` | PATCH | Update status/endedAt/durationSeconds; `?agencyId=` |
| `/v1/recordings/{id}/audio` | POST | Upload WAV as `application/octet-stream`; streamed, no full-file RAM |
| `/v1/recordings/{id}/transcript` | POST | Stub — not viable on-device |

Pre-flight check (no firmware needed):
```bash
curl -H "Authorization: Bearer <KEY>" https://bp-core-demo-orchestrator.azurewebsites.us/v1/models
```

## Entra ID (Azure AD)

Tenant: `a85d2dcc-2ae1-4bb9-bfe2-b4d4f5eb8910`

| App registration | Application ID |
|---|---|
| `bluepartner-spa-dev` (React SPA) | `b4fd7c41-f926-44e8-8685-a48990eb2db0` |
| `bluepartner-api-dev` (backend) | `7c8cb1ee-710c-4b57-a58a-d0f8e3b778b1` |

API scope: `api://7c8cb1ee-710c-4b57-a58a-d0f8e3b778b1/access_as_user`

Application roles (defined on `bluepartner-api-dev`): `System.Admin`, `Agency.Admin`, `Reviewer`, `Officer`

The firmware uses OAuth 2.0 **client credentials** flow against `login.microsoftonline.us` (Azure Government). The token is cached in `s_accessToken[]` with an expiry guard (`ensureToken()` re-fetches 60 s before expiry). This is separate from the React SPA's MSAL interactive flow.

## TLS policy

- **`WiFiClientSecure::setCACert()`** always. `setInsecure()` is never used.
- Two TLS endpoints require trust roots: `bp-core-demo-orchestrator.azurewebsites.us` and `login.microsoftonline.us`. Both are Azure Government — verify the actual root chain for each with `openssl s_client -connect <host>:443 -showcerts`. Government cloud may use DigiCert Global Root CA (original) or Microsoft RSA Root CA 2017 — do not assume commercial G2/G3 roots apply.
- Do not hand-type or invent certificate text — one wrong character causes a silent handshake failure.
- `BlueClient` reuses a single `WiFiClientSecure _tls` member across all API calls within a recording session to amortize TLS handshake cost.
- NTP sync is mandatory before any TLS call. A freshly booted ESP32 thinks it's 1970; every cert looks "not yet valid" until the clock is set. `syncNTP()` blocks until `tm_year ≥ 2024`.

## Retry policy

| Status | Action |
|---|---|
| `200` / `201` | Success |
| `401 / 403 / 404` | Fail fast, print response body |
| `502` or transport error (negative) | Exponential backoff, up to 5 retries, cap 30 s |

Both `main.cpp` (for the smoke test) and `BlueClient` (for all recordings endpoints) implement this independently. `BlueClient` uses `isRetryable()` internally.

## Architecture

`main.cpp` is a flat Arduino sketch (`setup` / `loop`) for the smoke-test milestone. The broader firmware is structured around two classes:

**`BlueClient`** (`BlueClient.h/.cpp`)
- One instance per device lifetime.
- Holds a session state machine: `IDLE → RECORDING → UPLOADING → COMPLETE | FAILED`.
- Wraps `createRecording`, `updateRecording`, `uploadAudio`, `uploadTranscript` (stub).
- Uses a shared `WiFiClientSecure _tls` socket with `setReuse(true)` across calls.
- `agencyId` / `officerId` are passed per-call — TODO: read from NVS during provisioning.

**`AudioCapture`** (`AudioCapture.h/.cpp`)
- Installs the I2S driver for an INMP441 microphone (legacy `driver/i2s.h` API).
- Writes a zeroed 44-byte WAV placeholder at `startRecording()`, streams PCM samples to SD card via `processAudio()`, then patches RIFF/data chunk sizes at `stopRecording()`.
- `openForUpload()` returns an `fs::File` at position 0, ready to pass directly to `BlueClient::uploadAudio()`.
- **Not yet wired into `main.cpp`** — next milestone.

FreeRTOS is running underneath. Future: move `processAudio()` into a dedicated task pinned to core 0; add PTT GPIO state machine.

## Audio chain

I2S driver: INMP441 PDM mic → I2S_NUM_0 → DMA buffers → 32-bit reads → strip high bit → 16-bit PCM → SD card WAV.

Parameters (see `AudioConfig.h`):
- 16 kHz, mono, 16-bit PCM — 32 kB/s uncompressed
- DMA: 8 buffers × 512 samples; `processAudio()` must run faster than ≈256 ms to avoid overflow
- WAV header: standard 44-byte RIFF, patched in-place at stop

Pin assignments (finalized 2026-07-01 from breadboard wiring):

| Symbol | GPIO | Notes |
|---|---|---|
| `I2S_SCK_PIN` | 4 | INMP441 SCK + MAX98357A BCLK (shared bus) |
| `I2S_WS_PIN` | 5 | INMP441 WS + MAX98357A LRC (shared bus) |
| `I2S_SD_PIN` | 6 | INMP441 SD (mic data in) |
| `I2S_DOUT_PIN` | 7 | MAX98357A DIN (speaker data out) |
| `SD_CS_PIN` | **TBD** | Was GPIO 5 — conflicts with `I2S_WS_PIN`; assign a free GPIO |

MAX98357A passive connections (no GPIO): GAIN → 100 kΩ → GND (sets +9 dB); SD → 100 kΩ → Vin (always enabled). Both ICs use standard 1 µF + 0.1 µF bypass cap bridges on VDD/GND.

Audio format is WAV by default (`AUDIO_FORMAT_CURRENT = AUDIO_FMT_WAV`). An `AUDIO_FMT_OPUS` placeholder is defined but not implemented. Confirm with backend team whether compressed upload is preferred before switching.

## Conventions

- `Serial.printf()` for all debug output — not `String` formatting or `print(String(...))`
- `#define` macros in `secrets.h` for all per-device config; referenced as string-literal concatenation in C++ code (e.g. `"Bearer " BACKEND_API_KEY`)
- `StaticJsonDocument` for stack-allocated JSON; `DynamicJsonDocument` only for token responses (large, infrequent)
- PSRAM available (`-DBOARD_HAS_PSRAM`) for large audio buffers; no dynamic allocation policy established yet
