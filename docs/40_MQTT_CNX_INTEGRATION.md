# 40 — MQTT / CNX Integration Spec (SVC-015)

Status: **Ready for Claude implementation planning**  
Owner: ChatGPT/Codex architecture, Claude firmware implementation  
Scope: Optional network integration layer for CNX Smart Pool Villa / third-party
villa dashboards. The standalone controller remains authoritative and safe when
networking is disabled or offline.

## 1. Goals

- Let a CNX Smart Pool Villa app, dashboard, or bridge observe and command the
  SVC controller without coupling firmware to one vendor app.
- Keep SVC-100 fully standalone: relay safety, presence logic, local rules, OTA
  health, and provisioning must work without MQTT, broker, cloud, or CNX.
- Provide a stable MQTT topic contract that a CNX bridge, Home Assistant,
  Node-RED, or custom gateway can consume.
- Reuse the same security posture as Web UI: no unauthenticated output control,
  no password in URL, bounded JSON, and fail-closed command handling.

## 2. Non-Goals

- No cloud dependency in firmware.
- No direct CNX app reverse-engineering in firmware.
- No bypass around the local control engine or relay safe-off.
- No Home Assistant requirement; HA is only one possible bridge.
- No plaintext secrets in published state topics.

## 3. Architecture

```
CNX app / dashboard
        |
        | app API, webhook, or MQTT client
        v
CNX bridge / broker / Node-RED / HA  <---- optional external layer
        |
        | MQTT over LAN/VPN
        v
SVC MQTT adapter (optional service)
        |
        | narrow internal command/status API
        v
control / relay / dinput / presence / health
```

The SVC device is the source of truth for physical safety. Remote commands are
requests, not authority. A command can be rejected because the device is
unprovisioned, unauthenticated, stale, in fault, out of range, or blocked by a
local safety rule.

## 4. Configuration Model

SVC-015 should add schema v6 fields after the current v5 block. Keep legacy v1..v5
layouts frozen and migrate forward intentionally.

Recommended fields:

```c
#define SVC_MQTT_HOST_MAX      64
#define SVC_MQTT_USER_MAX      64
#define SVC_MQTT_PASS_MAX      64
#define SVC_MQTT_PREFIX_MAX    64
#define SVC_MQTT_CLIENT_ID_MAX 48

uint8_t mqtt_enabled;
char    mqtt_host[SVC_MQTT_HOST_MAX];
uint16_t mqtt_port;                 /* default 1883, 8883 if TLS */
char    mqtt_user[SVC_MQTT_USER_MAX];
char    mqtt_pass[SVC_MQTT_PASS_MAX];
char    mqtt_client_id[SVC_MQTT_CLIENT_ID_MAX];
char    mqtt_topic_prefix[SVC_MQTT_PREFIX_MAX];
uint8_t mqtt_tls;                   /* 0/1 */
uint8_t mqtt_allow_remote_control;  /* default 0 until explicitly enabled */
uint16_t mqtt_command_timeout_ms;   /* stale command guard, default 5000 */
```

Sanitize rules:

- `mqtt_enabled`, `mqtt_tls`, and `mqtt_allow_remote_control` normalize to 0/1.
- `mqtt_host`, `mqtt_user`, `mqtt_pass`, `mqtt_client_id`,
  `mqtt_topic_prefix` are forced NUL-terminated.
- `mqtt_port` clamps to 1..65535, default 1883.
- Empty `mqtt_topic_prefix` defaults to
  `svc/<board_id>/<device_name-or-client-id>`, sanitized to `[a-zA-Z0-9_-]`.
- `mqtt_pass` is write-only from APIs: never returned by GET.
- Remote control defaults disabled even when MQTT telemetry is enabled.

Web UI settings whitelist may add MQTT installer fields, but must still exclude
`setup_password`, `provisioned`, `webui_require_auth`, board identity, relay
polarity, and relay safe-state.

## 5. Topic Contract

Default base prefix:

```
svc/<board_id>/<client_id>
```

Example:

```
svc/svc100_reva/villa12
```

### Published State

All JSON must be bounded and escaped. State topics should be retained where useful
for dashboards, but command topics must not be retained.

| Topic | Retain | Payload |
|---|---:|---|
| `<base>/availability` | yes | `online` / `offline` via MQTT LWT |
| `<base>/status` | yes | board, firmware, uptime, provisioned, health |
| `<base>/io` | yes | relay/input masks, counts, labels |
| `<base>/presence` | yes | fused presence state + per-sensor summary |
| `<base>/health` | yes | health status, fault latched, dropped events |
| `<base>/config/public` | yes | non-secret installer-visible public config |
| `<base>/event` | no | edge/event stream for debugging or audit |
| `<base>/cmd_ack` | no | command result, correlation id |

Example status:

```json
{
  "schema": 1,
  "board_id": "svc100_reva",
  "device_name": "villa12",
  "fw": "0.1.0",
  "uptime_ms": 123456,
  "health": "ok",
  "fault": false,
  "provisioned": true
}
```

Example IO:

```json
{
  "schema": 1,
  "relay_count": 4,
  "input_count": 8,
  "relays": 0,
  "inputs": 3,
  "relay_labels": ["Pool pump", "Pool light", "Garden", "Spare"],
  "input_labels": ["Door", "Keycard", "Leak", "Smoke"]
}
```

## 6. Command Contract

Remote commands arrive on:

```
<base>/cmd
```

Payload:

```json
{
  "id": "cnx-20260703-0001",
  "type": "relay_set",
  "relay": 1,
  "on": true,
  "source": "cnx",
  "ts_ms": 123456
}
```

Required fields:

- `id`: bounded string, echoed in `cmd_ack`.
- `type`: command type.
- `source`: bounded string such as `cnx`, `ha`, `node-red`, `local-bridge`.

Supported phase-1 command types:

| Type | Fields | Behavior |
|---|---|---|
| `relay_set` | `relay`, `on` | Request one relay state. Must pass remote-control gate. |
| `scene` | `name` | Maps named villa scene to local rules/config in a later phase. Phase 1 may reject with `unsupported`. |
| `config_reload` | none | Ask services to republish config/status; no mutation. |
| `ping` | none | Reply with `ok` in `cmd_ack`. |

Acknowledgement topic:

```
<base>/cmd_ack
```

Example ACK:

```json
{
  "schema": 1,
  "id": "cnx-20260703-0001",
  "ok": false,
  "error": "remote control disabled",
  "relay_state": 0
}
```

## 7. Remote-Control Gate

Every physical-output command must pass all gates:

1. Device is provisioned.
2. MQTT is enabled and connected.
3. `mqtt_allow_remote_control == 1`.
4. Broker authentication succeeded, if configured.
5. Payload parses fully as bounded JSON.
6. Command type is known.
7. Relay/input index is in range for the active HAL profile.
8. Device health does not have a latched fault that requires safe-off.
9. Command is not stale if `ts_ms` is present and outside
   `mqtt_command_timeout_ms`.
10. Local failsafe still wins: if control logic or presence fault applies
    safe-off, MQTT must not re-energize relays against that decision.

Recommended implementation stance:

- MQTT adapter should not call low-level GPIO/HAL directly.
- It may call the same service-level relay command path as Web UI only after the
  gates pass, or better, post a bounded command event for control to arbitrate.
- Rejected commands publish `cmd_ack` with an error. They never partly apply.

## 8. CNX App Mapping

Because public details for "Smart Pool Villa by CNX" are not available in this
repo, firmware should expose a generic MQTT contract. A CNX bridge maps app
concepts to SVC topics.

Suggested villa objects:

| CNX object | SVC mapping |
|---|---|
| Pool pump | relay label / relay command |
| Pool light | relay label / relay command |
| Garden light | relay label / relay command |
| Keycard | digital input / event |
| Door | digital input / event |
| Leak/smoke | digital input / health/fault event |
| Occupancy | fused presence |
| Check-in scene | local scene command or config profile |
| Check-out scene | local scene command or config profile |
| Pool service mode | scene + remote-control time window |

The bridge owns CNX-specific auth/API details. SVC firmware only sees MQTT
credentials and validated command payloads.

## 9. Failure Behavior

- Broker unavailable: SVC remains standalone; publish `offline` via LWT if the
  broker supports it.
- MQTT disconnect: stop accepting remote commands; keep local logic running.
- Bad payload: reject and ACK with `bad json` or `invalid command`.
- Queue full: reject or drop event; increment diagnostic counter.
- Health fault: publish health/fault and reject output commands.
- Reboot: reconnect, republish retained state, keep relays safe through boot.

## 10. Implementation Plan for Claude

### Phase 0 — Docs and config only

- Add schema v6 MQTT fields and migration v5->v6.
- Add sanitizer coverage and host tests.
- Add Web UI whitelist fields for MQTT settings.
- Ensure `GET /api/config` strips `mqtt_pass`.
- Keep `mqtt_enabled = 0` by default.

### Phase 1 — Telemetry publisher

- Add `components/mqtt_bridge`.
- Use ESP-IDF `mqtt` component already available in the build.
- Connect only after network is up and config is provisioned enough to have
  broker settings.
- Publish `availability`, `status`, `io`, `presence`, `health`, and selected
  `event` messages.
- No remote relay control yet.

### Phase 2 — Command subscriber

- Subscribe to `<base>/cmd`.
- Implement `ping`, `config_reload`, and gated `relay_set`.
- Add host-testable pure parser/validator for command JSON.
- Add ACK publisher.
- Remote control default stays off.

### Phase 3 — CNX bridge profile

- Document CNX-side mapping once the CNX app/API is known.
- Add optional topic aliasing if CNX requires a fixed topic shape.
- Keep firmware generic; put CNX-specific translation in bridge/gateway when
  possible.

## 11. Required Tests

Host tests:

- MQTT config migration v5->v6 preserves provisioning and existing fields.
- Sanitizer clamps port and booleans, NUL-terminates strings, defaults prefix.
- `mqtt_pass` is write-only and not returned by config GET.
- Command parser rejects malformed, oversized, unknown, stale, out-of-range, and
  retained commands.
- Remote command gate denies unprovisioned, remote-control-disabled, faulted, and
  out-of-range relay commands.
- ACK JSON is bounded and escaped.

Bench tests:

- Boot with MQTT disabled: standalone behavior unchanged.
- Broker down: controller boots and controls locally.
- Broker up: publishes retained state and LWT.
- Remote command rejected until `mqtt_allow_remote_control=1`.
- Valid `relay_set` works only after auth/config gate and is visible in Web UI.
- Presence stale/fault safe-off blocks remote re-energize.
- Reboot/OTA keeps relay safe-off and republishes state.

## 12. Security Checklist

- No passwords in URL or topic names.
- `mqtt_pass` never appears in GET `/api/config`, logs, status, or MQTT state.
- Mutating Web UI API remains auth+CSRF guarded.
- MQTT command parser is bounded; no dynamic unbounded string operations.
- TLS is supported by config, but plaintext LAN MQTT remains allowed only as a
  trusted-LAN deployment mode.
- Remote control is explicit opt-in.
- Local safety and failsafe always override remote commands.

