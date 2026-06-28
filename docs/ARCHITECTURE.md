# SVC-100 Firmware Architecture

## 1. Design goals

1. **Standalone first.** All core control (presence + inputs -> relays) must run with
   no cloud, no broker, and no Home Assistant. Network features are strictly optional
   add-ons layered on top of a working local controller.
2. **Deterministic & watchdog-friendly.** No `delay()`, no blocking super-loops. Every
   task either blocks on a queue/event or runs a bounded amount of work then yields.
   The Task Watchdog (TWDT) subscribes the control task.
3. **Static after boot.** Memory is allocated during init. After `app_main` finishes
   bring-up, the steady state does no heap allocation on the hot path.
4. **Module isolation.** Modules talk through the **event bus** and narrow public APIs,
   never by reaching into each other's internals. Each module is an ESP-IDF component
   with its own `include/` header(s) and source file(s).
5. **Testable.** Hardware access is behind thin HAL seams (`board`, `rs485`) so logic
   modules (`control`, `modbus_master`, `dinput` debounce) can be host-tested.

## 2. Layered model

```
+-------------------------------------------------------------+
|  Application / wiring        main/app_main.c                 |
+-------------------------------------------------------------+
|  Services        control | webui | netmgr | presence | ...  |
+-------------------------------------------------------------+
|  Protocol        modbus_master | (mqtt) | (rest)            |
+-------------------------------------------------------------+
|  Drivers         relay | dinput | rs485 | indicator | button|
+-------------------------------------------------------------+
|  Platform        board (pin map + HAL init) | svc_common     |
+-------------------------------------------------------------+
|  ESP-IDF 5.x / FreeRTOS / esp_driver_*                       |
+-------------------------------------------------------------+
```

A module may depend only on modules at the same layer or below. Cross-cutting
notifications flow **upward** through the event bus, commands flow **downward**
through direct API calls.

## 3. Concurrency model

Each long-lived activity is a FreeRTOS task with a clear single responsibility.
Tasks never busy-wait; they block on a queue, event group, or driver event.

| Task            | Prio | Stack  | Core | Blocks on                         |
|-----------------|------|--------|------|-----------------------------------|
| `control`       | 6    | 4096   | 1    | event bus queue (TWDT subscribed) |
| `dinput`        | 5    | 3072   | 1    | GPIO ISR notify + debounce tick   |
| `modbus`        | 5    | 4096   | 1    | request queue / UART event        |
| `presence`      | 4    | 3072   | 1    | poll timer -> modbus request      |
| `indicator`     | 3    | 2560   | 0    | indicator command queue           |
| `button`        | 4    | 2560   | 0    | GPIO ISR notify + long-press tick |
| `webui` (httpd) | 4    | 6144   | 0    | sockets (managed by esp_http_srv) |
| `netmgr`        | 4    | 4096   | 0    | system event loop                 |

Priorities keep real-time IO (inputs, relays, modbus) above networking. Networking
lives on core 0 with Wi-Fi; deterministic control lives on core 1.

## 4. Event bus

A single FreeRTOS queue of fixed-size `svc_event_t` messages decouples producers from
consumers. The control task is the primary consumer. Producers (`dinput`, `presence`,
`button`, `netmgr`) post events; they never call `control` directly. This keeps the
dependency graph acyclic and makes the control logic unit-testable by feeding it
synthetic events.

```
dinput ──┐
presence ─┼──> [ event bus queue ] ──> control ──> relay
button ──┤                                    └──> indicator
netmgr ──┘
```

Events are value types (no pointers to transient data), so no ownership/lifetime
issues and no post-boot allocation.

## 5. Configuration & persistence

`storage` wraps NVS. Configuration is a single versioned, packed struct
(`svc_config_t`) read once at boot into RAM. Writes are explicit and rate-limited.
A schema `version` field drives forward migration. Factory reset (long-press Reset)
erases the config namespace and reboots to defaults.

## 6. Failsafe & safety

- Relays initialize to a configured **safe state** before any logic runs.
- If the control task detects loss of a critical input source (e.g., modbus presence
  sensor offline beyond a timeout), it applies the configured failsafe relay state.
- The Task Watchdog resets the device if the control loop stalls. OTA uses the ESP-IDF
  app-rollback mechanism: a new image must call `esp_ota_mark_app_valid_cancel_rollback()`
  after self-check, else the bootloader reverts.

## 7. Boot sequence (app_main)

1. `svc_common` log/init, print banner + firmware/version/partition.
2. `board_init()` — clocks, GPIO directions, I2C, relay safe state.
3. `storage_init()` + `storage_load(&config)`.
4. `eventbus_init()`.
5. Bring up drivers: `indicator`, `relay`, `dinput`, `rs485`, `button`.
6. Bring up protocol/services: `modbus_master`, `presence`, `control`.
7. Start the control task (TWDT subscribed).
8. Optionally start `netmgr` -> `webui` (+ MQTT) if enabled in config.
9. Mark OTA image valid after a successful self-test window.

Failure of an optional service is logged and degraded gracefully; failure of a core
driver during boot drives the indicator to a fault pattern and (optionally) reboots.

## 8. Error handling convention

Public functions return `svc_err_t` (a thin alias over `esp_err_t` with project codes).
The `SVC_RETURN_ON_ERR` / `SVC_GOTO_ON_ERR` macros centralize check-log-propagate so
no error is silently dropped. ISRs never log or allocate; they only notify tasks.
