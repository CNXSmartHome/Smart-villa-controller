# SVC-100 Module List

Every module is an ESP-IDF component: `components/<name>/{include/<name>.h, <name>.c, CMakeLists.txt}`.
Status legend: **[impl]** functional skeleton that compiles & runs the happy path,
**[stub]** API + structure present, body returns `SVC_ERR_NOT_IMPLEMENTED` where a
hardware/feature detail is intentionally deferred.

| Module          | Layer     | Status | Public surface (summary)                                   | Depends on               |
|-----------------|-----------|--------|------------------------------------------------------------|--------------------------|
| `svc_common`    | platform  | impl   | `svc_err_t`, result/log macros, `svc_clamp`, version       | —                        |
| `board`         | platform  | impl   | `board_init`, pin map, `board_relay_safe_state`, I2C handle | svc_common, driver       |
| `eventbus`      | platform  | impl   | `eventbus_init/post/post_isr/receive`, `svc_event_t`        | svc_common               |
| `indicator`     | driver    | impl   | `indicator_start`, `indicator_set(pattern)`, `_beep`       | board, eventbus          |
| `button`        | driver    | impl   | `button_start`, emits short/long-press events              | board, eventbus          |
| `relay`         | driver    | impl   | `relay_init`, `relay_set/get`, `relay_apply_safe`          | board                    |
| `dinput`        | driver    | impl   | `dinput_start`, debounced state + edge events              | board, eventbus          |
| `rs485`         | driver    | impl   | `rs485_open`, `rs485_txn` (half-duplex DE/RE)              | board                    |
| `modbus_master` | protocol  | impl   | `mb_master_init`, `mb_read_holding/input`, `mb_write_*`    | rs485, svc_common        |
| `presence`      | service   | impl   | `presence_start`, polls sensor, emits presence events      | modbus_master, eventbus  |
| `storage`       | service   | impl   | `storage_init/load/save`, `svc_config_t` schema            | svc_common (nvs_flash)   |
| `netmgr`        | service   | stub   | `netmgr_start` (Wi-Fi STA/AP + optional Eth), status event | storage, eventbus        |
| `webui`         | service   | stub   | `webui_start`, REST `/api/*`, static UI, SSE status        | netmgr, control, storage |
| `control`       | service   | impl   | `control_start`, rule engine: events -> relay/indicator    | eventbus, relay, presence, dinput, storage |

## Module responsibilities

### svc_common
Foundational types shared by all modules: `svc_err_t` (aliases `esp_err_t` + project
error space starting at `SVC_ERR_BASE`), the `SVC_RETURN_ON_ERR`/`SVC_GOTO_ON_ERR`
macros, a per-module `TAG` logging convention, small math helpers, and the build
version string. No hardware dependencies; host-compilable.

### board
The single source of truth for **pin assignments** and low-level bring-up. Owns the
shared I2C bus (RTC), configures relay/input/RS485-DE GPIOs, and exposes accessor
handles. Changing hardware revisions touches only this module.

### eventbus
Fixed-size message queue carrying `svc_event_t`. Provides task-context and ISR-context
post functions. The decoupling backbone of the system (see ARCHITECTURE §4).

### indicator
Owns the status RGB LED (`led_strip` / RMT) and the buzzer (LEDC PWM). Driven by a
command queue so callers never block. Renders named patterns (boot, ok, fault,
config, ota) and short beeps.

### button
Debounces Config and Reset buttons via GPIO ISR + a short software timer. Emits
`EVT_BUTTON_SHORT` / `EVT_BUTTON_LONG`. Reset long-press triggers factory reset,
Config long-press toggles AP/provisioning mode.

### relay
4-channel dry-contact output driver. Tracks logical state, applies the configured
active-high/active-low polarity, and exposes an atomic `relay_apply_safe()` used at
boot and on failsafe.

### dinput
8-channel opto-isolated input driver. ISR-on-change wakes a debounce routine
(configurable window). Publishes stable level + rising/falling edge events. Pure
debounce logic is split out for host testing.

### rs485
Half-duplex RS485 transport over a UART with DE/RE direction control (auto via
`uart_set_rts` hardware flow or manual GPIO). Provides a single transactional
`rs485_txn(req, req_len, resp, resp_cap, timeout)` primitive with line-turnaround and
inter-frame timing. Two instances (RS485-A / RS485-B).

### modbus_master
Modbus RTU master built on `rs485`. CRC-16, function codes 0x03/0x04/0x06/0x10,
exception decoding, retry policy. Serialized per bus by an internal mutex so multiple
services can share one RS485 line safely.

### presence
mmWave human-presence service. Periodically polls the sensor's holding/input registers
via `modbus_master`, normalizes to `present / absent / unknown(+stale)`, and emits
`EVT_PRESENCE_CHANGED`. Sensor register map is config-selectable.

### storage
NVS-backed configuration. Versioned packed `svc_config_t`, load-once/save-explicit,
forward migration, factory defaults, factory-reset erase.

### netmgr (stub)
Connection manager: Wi-Fi STA with AP fallback for provisioning, optional Ethernet.
Publishes `EVT_NET_UP` / `EVT_NET_DOWN`. SNTP time sync hook. Stubbed bring-up so the
core controller compiles and runs without networking.

### webui (stub)
`esp_http_server` exposing a static local UI plus a REST API (`/api/status`,
`/api/io`, `/api/config`, `/api/ota`) and a status stream. OTA endpoint wraps
`esp_https_ota` / `esp_ota_*`. Stubbed handlers registered and documented.

### control
The standalone brain. Consumes the event bus, evaluates configured rules
(presence + input edges + timers) and drives relays and the indicator. Applies
failsafe on source loss. No networking dependency — runs first and alone.
