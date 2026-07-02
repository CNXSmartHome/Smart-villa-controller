# SVC-100 UI Simulator v2

A dependency-free (plain Node + HTML/CSS/JS) preview of the future embedded Web UI.
It serves a single-page app and mock ESP REST endpoints so UX, auth/CSRF, and the
control/rules/network/OTA flows can be exercised before they are ported back into
firmware. **It is a simulator only — it drives no hardware and does not change the
embedded firmware UI.**

## Run

```bash
node tools/ui-simulator/server.js
# then open:
open http://127.0.0.1:3200/
```

- Default port: `3200` (override with `PORT`).
- Default setup password: `admin1234` (override with `SVC_SIM_PASSWORD`).

## Tabs

1. **Dashboard** — presence, room occupied/empty/unknown, health, fault, network,
   MQTT state, uptime, last event.
2. **Control** — relay 1–4 ON/OFF (authenticated `POST /api/io`) and DI 1–8 live
   status (polled `GET /api/io`).
3. **Rules** — editable rows: trigger, channel, FOR delay (ms), action ON/OFF,
   target relay, OFF delay (ms). Saved as a JSON `rules` array to `POST /api/config`.
4. **Network / MQTT** — enable, broker host/port, username, password, topic prefix,
   TLS toggle, and a **Test connection** button (`POST /api/mqtt/test`).
5. **Settings** — device / Wi-Fi / presence-sensor settings (the original simulator
   flow, preserved; urlencoded `POST /api/config`).
6. **OTA** — firmware URL, Start update, health-gate status, rollback status
   (`POST /api/ota`).

## Mock API

| Method | Route | Auth | CSRF | Notes |
|--------|-------|------|------|-------|
| GET  | `/api/status`     | no  | no  | Public, no secrets (parity with firmware). |
| GET  | `/api/csrf`       | yes | –   | Requires `X-Auth-Token`. |
| GET  | `/api/config`     | yes | –   | Secrets stripped (`wifi_pass`, `mqtt_pass`, `setup_password`). |
| POST | `/api/config`     | yes | yes | Whitelist writes; protected fields rejected (fail closed). |
| GET  | `/api/io`         | yes | –   | Board info + live relay/input masks. |
| POST | `/api/io`         | yes | yes | `?relay=N&on=0|1`; range-checked. |
| POST | `/api/mqtt/test`  | yes | yes | Simulated broker probe. |
| POST | `/api/ota`        | yes | yes | Simulated health-gated update. |

## Security flow (mirrors the firmware posture)

- Sign in with the setup password; it is sent only as the `X-Auth-Token` **header**
  (in the request body path for POSTs) — **never** in a URL or query string.
- `GET /api/csrf` requires `X-Auth-Token`; it returns the per-boot CSRF token.
- Every mutating `POST` requires **both** `X-Auth-Token` and `X-SVC-CSRF`.
- Privileged reads (`GET /api/config`, `GET /api/io`) require `X-Auth-Token`.
- Protected/secret fields are never returned by `GET /api/config`, and a config
  write can never flip `provisioned`, `webui_require_auth`, or board identity.
- There is no LAN provisioning path in the simulator (provisioning is out of scope).

## Notes

- Runtime state (relay mask, inputs, presence, OTA status) is in-memory and resets
  on restart; config edits persist for the life of the process.
- Verify before commit: `node --check tools/ui-simulator/server.js`.
