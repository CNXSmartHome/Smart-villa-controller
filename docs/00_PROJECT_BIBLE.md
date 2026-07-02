# 00 — Project Bible (Source of Truth)

Smart Villa Controller **SVC-100** / Smart Villa OS.

This document is the highest authority for the project. If any other document,
comment, or instruction conflicts with this file, **this file wins**. Changes to
this file require Owner approval.

## 1. Product

A standalone industrial controller for pool villas, hotels, and smart homes,
built on the **ESP32-S3-WROOM-1-N16R8** with **ESP-IDF 5.x** and FreeRTOS.

The device must run its core control **fully standalone** — no cloud, no broker,
and no Home Assistant required. Network features (Web UI, MQTT, REST) are
optional layers added on top of a controller that already works offline.

### Core functions

- Read an mmWave human-presence sensor over RS485 Modbus RTU.
- Read 8 dry-contact digital inputs (door, keycard, leak, smoke, mmWave S1/S2,
  manual override).
- Drive 4 dry-contact relay outputs with a guaranteed safe (de-energized) state.
- A local rule/logic engine that maps inputs to outputs and works offline.
- A local Web UI for status, manual override, and configuration.
- OTA firmware update validated by health checks (not a fixed delay), with
  bootloader rollback.

### Hardware baseline (Rev A)

12–24 VDC input; ESP32-S3; RS485 ×2; relay dry-contact ×4; digital input ×8;
Wi-Fi (Ethernet optional); USB-C; RTC; buzzer; status LED; config + reset
buttons; DIN-rail form factor. The authoritative GPIO map is
`components/board/include/board.h`; the human-readable mirror is
`docs/20_PIN_ASSIGNMENT_REV_A.md` and `hardware/pin_database/`.

## 2. Non-negotiable engineering rules

These are absolute. Code that violates them must not merge.

1. No relay or physical-output control without authentication after provisioning.
2. No provisioning from normal LAN mode (physical config button at boot or AP
   provisioning mode only).
3. No password in a URL/query string.
4. All JSON output must be safely escaped and bounded.
5. No `delay()` and no blocking forever in a production control path.
6. Relay safe state is **de-energized** and must hold from power-on through
   reset, brown-out, and OTA.
7. OTA images are validated by health checks, then marked valid — never by a
   fixed timer.
8. ESP-IDF only. No Arduino. No new third-party libraries without approval.

## 3. Terminology

- **Provisioning** — first-time setup that establishes the admin/setup password.
- **Provisioning mode** — the explicit state in which provisioning is allowed
  (config button held at boot, or AP provisioning mode). Never ambient LAN.
- **Safe state / safe-off** — relays de-energized; the deterministic state the
  device drives at boot and on any fault.
- **Failsafe** — the action taken when a critical input source is lost
  (e.g. presence sensor offline): apply the safe state.
- **Standalone** — operating with no network; the mandatory baseline.
- **Logic engine** — the data-driven IF / FOR / THEN rule evaluator that turns
  inputs (presence, digital inputs, timers) into relay actions.

## 4. Roles (multi-agent workflow)

- **ChatGPT — CTO / architecture.** Owns architecture, hardware block design,
  BOM, and sprint planning.
- **Claude — Firmware Engineer.** Implements assigned firmware tasks in ESP-IDF.
  Does not redesign architecture or add libraries without approval.
- **Codex — Principal Reviewer.** Reviews for security, reliability, races,
  memory safety; returns concrete findings/patches; marks bench-test safety.
- **Gemini — UI / Marketing.** Mockups, diagrams, assets, copy — must follow the
  product vision and terminology here.

Process: no implementation without a task ID (`AI_TASK_QUEUE.md`); security and
relay-control changes require explicit review; record edits in
`.ai/TASK_LOCKS.md`.

## 5. Architecture (summary)

Layered, event-driven, static-after-boot. See
`engineering/03_Architecture_Decision_Record/` and the firmware architecture doc
for detail.

```
Application (app_main)
Services      control | logic | webui | netmgr | presence | health
Protocol      modbus_master
Drivers       relay | dinput | rs485 | indicator | button
Platform      board (pin map + HAL) | storage(config) | eventbus | svc_common
ESP-IDF 5.x / FreeRTOS
```

Producers post value-type events to a fixed-size **event bus**; the control task
is the only consumer and the standalone brain. Modules talk through narrow APIs
and the bus, never by reaching into each other's internals.

## 6. Safety posture

- Relays de-energized by `board_init`, by `relay_apply_safe`, and by
  `svc_config_sanitize` forcing `relay_safe_on = 0` — three independent layers.
- Every loaded configuration is sanitized after its CRC check (bounds, booleans,
  NUL-termination, auth-forced-when-provisioned).
- Bounded waits everywhere in control-critical paths; the Task Watchdog covers
  the control loop.
- Open hardware/firmware risks are tracked as **Release Gates**
  (`docs/RELEASE_GATES.md`) and must be closed before production.

## 7. Definition of done

A task is Done when: it satisfies the rules above; it builds under ESP-IDF for
`esp32s3`; host unit tests pass (`tests/host/run.sh`); docs are updated; and,
for security/relay-control changes, Codex has signed off and bench-test safety
is stated.
