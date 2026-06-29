# BP Dev Board — CLAUDE.md

## Project

Firmware for the **Blue Partner Body-Worn Capture Device** — an ESP32-S3 worn by patrol officers. Communicates with a production Azure Function App backend over HTTPS. CJIS context: TLS validation is mandatory everywhere.

Current milestone: round-trip connection smoke test (`GET /v1/models`).

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
  main.cpp            — all application logic (single TU for now)
  secrets.h           — real credentials; gitignored, never committed
  secrets.h.example   — placeholder template; committed
platformio.ini
.gitignore            — includes src/secrets.h
CLAUDE.md
```

## Secrets discipline

All credentials and the TLS CA bundle live in `src/secrets.h` as `#define` macros:

| Macro | Contents |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | Bench/patrol WiFi |
| `BACKEND_BASE_URL` | `https://<app>.azurewebsites.net` |
| `BACKEND_API_KEY` | Shared bearer key (`POST_PROCESS_API_KEY`) |
| `DIGICERT_CA_BUNDLE` | DigiCert Global Root G2 + G3 PEMs concatenated |

`src/secrets.h` is listed in `.gitignore`. `src/secrets.h.example` carries identical structure with placeholder values and is committed.

## Backend API

- Base URL: `https://bp-core-demo-orchestrator.azurewebsites.us` (Azure Government)
- All routes prefixed `/v1`
- Auth: `Authorization: Bearer <BACKEND_API_KEY>` on every request
- Milestone endpoint: `GET /v1/models` → JSON list of allowed model IDs

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

The firmware uses a static bearer key (`BACKEND_API_KEY`), not MSAL tokens — Entra ID token flow is for the React SPA only at this stage.

## TLS policy

- **`WiFiClientSecure::setCACert()`** always. `setInsecure()` is never used.
- The backend is on Azure Government (`*.azurewebsites.us`). Verify the actual root CA with `openssl s_client -connect bp-core-demo-orchestrator.azurewebsites.us:443 -showcerts` before populating `DIGICERT_CA_BUNDLE`. Government cloud may chain to DigiCert Global Root CA (original) or Microsoft RSA Root CA 2017 — do not assume the commercial G2/G3 roots apply.
- Do not hand-type or invent certificate text — one wrong character causes a silent handshake failure.
- NTP sync is mandatory before any TLS call. A freshly booted ESP32 thinks it's 1970; every cert looks "not yet valid" until the clock is set. `syncNTP()` blocks until `tm_year ≥ 2024`.

## Retry policy

| Status | Action |
|---|---|
| `200` | Success |
| `401 / 403 / 404` | Fail fast, print response body |
| `502` or transport error (negative) | Exponential backoff, up to 5 retries, cap 30 s |

## Architecture

Flat Arduino sketch (`setup` / `loop`) for milestone 1. FreeRTOS is running underneath — future milestones will add per-subsystem tasks (audio capture, PTT state, transmit gating).

## Audio chain (future milestone — not yet implemented)

ESP32-S3 has two I2S peripherals. Expected path: external codec or PDM mic → I2S → FreeRTOS audio task → encode → POST to backend. PTT GPIO gates TX/RX switching.

## Conventions

- `Serial.printf()` for all debug output — not `String` formatting or `print(String(...))`
- `#define` macros in `secrets.h` for all per-device config; referenced as string-literal concatenation in C++ code (e.g. `"Bearer " BACKEND_API_KEY`)
- No dynamic allocation policy established yet; PSRAM available for large audio buffers when needed
