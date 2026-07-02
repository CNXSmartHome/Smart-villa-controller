# AI Task Queue — Smart Villa OS / SVC-100

This file is the shared task queue for ChatGPT, Claude, Codex, Gemini, and future engineers.

## Rules

- Every AI must read `AI_START_HERE.md` and `docs/00_PROJECT_BIBLE.md` before working.
- No implementation without a task ID.
- Claude implements.
- Codex reviews.
- ChatGPT acts as CTO / architecture reviewer.
- Gemini handles UI/UX and marketing assets.
- Security and relay-control changes require explicit review.

---

## Status Legend

- `Planned`
- `Ready`
- `In Progress`
- `Review`
- `Bench Test`
- `Done`
- `Blocked`

---

## Sprint 1 — Bring-up and Core Safety

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-001 | Verify repo structure and developer toolkit | ChatGPT | Review | High | `make doctor` 11/18: missing Bible/Security/Coding docs (real gap); idf.py/cmake/ninja absent in CI sandbox → build on Mac. DR-0002/SVC-FW-001 |
| SVC-002 | ESP32-S3 board bring-up review | Claude | Review | High | Pin audit done (DR-0002). No pin DB existed → derived seed `hardware/pin_database/svc100_pin_database_rev_a.csv`. board.h avoids flash/PSRAM/USB pins. Opened RG-5/RG-6. No GPIO changed |
| SVC-003 | Relay driver safety review | Codex | Bench Test | High | Claude FW review PASS: safe-off in 3 layers, bounded waits, init cleanup. Awaiting Codex + RG-2/RG-6 |
| SVC-004 | Digital input debounce review | Claude | Review | Medium | PASS: debounce/dry-contact/eventbus OK; zero-tick impossible (triple-guarded) |
| SVC-005 | RS485 driver review | Codex | Bench Test | High | Claude FW review PASS: bounded timeouts, no portMAX_DELAY in prod, UART init cleanup, HW DE/RE timing. Awaiting Codex |
| SVC-006 | Modbus master validation review | Codex | Bench Test | High | Claude FW review PASS: length-before-payload, CRC, write echo, short-frame reject. Awaiting Codex |
| SVC-007 | Presence sensor integration plan | ChatGPT | Planned | High | MTD062/MTD262 |
| SVC-008 | Web API auth and CSRF review | Codex | Planned | Critical | Relay API protection |
| SVC-009 | OTA health-check validation | Claude | Planned | High | No fixed-delay validation |
| SVC-010 | Bench test procedure | ChatGPT | Review | High | Draft ready: `engineering/09_Bench_Test_Procedure.md` — gate procedure to validate logic-engine/v3/SVC-012 before flipping `CONFIG_SVC_USE_LOGIC_ENGINE`. Covers build matrix A/B, relay safe-off, NVS migration v1/v2→v3, FOR/linger/interlock, dry-contact fallback, OTA rollback, provisioning lockout, watchdog. Non-hazardous load only. Awaiting CTO/Codex execution + sign-off |

---

## Sprint 2 — mmWave and Logic

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-011 | mmWave RS485 register mapping | ChatGPT | Planned | High | Need final register map |
| SVC-012 | Dry contact fallback mode | Claude | Review | High | mmWave S1/S2 dry-contact presence fallback when RS485 stale. Schema v3 fields `fallback_din_enabled`/`fallback_din_chan`; pure `presence_effective()` in control_logic.c (RS485 authoritative while fresh; stale→DI; else UNKNOWN). 6 host tests PASS. Active only on the gated logic path. DR-0003. Awaiting Codex |
| SVC-013 | Logic engine v0.1 | Claude | Review | High | IF/FOR/THEN pure `components/logic` (Codex patches in: fail-closed + OFF-cancels-linger). **Schema v3** adds `for_ms`+`action` (+v1/v2→v3 migration; fixed v1 rule over-read). Adapter `control_logic.c` + **review-gated integration** into control.c behind `CONFIG_SVC_USE_LOGIC_ENGINE` (default OFF; legacy stays production). 76 host tests PASS under ASan/UBSan. DR-0003. Awaiting Codex review + on-target bench before flipping the flag |
| SVC-014 | Web UI settings page | Claude | Bench Test | Medium | **Codex FINAL APPROVE 2026-07-02** (auth/CSRF/body-only, whitelist fail-closed, bounded+escaped JSON, no password in URL; P3 constant-time `str_eq_ct` fixed). `idf.py build`/`ninja` PASS + host tests PASS. Bench-ready. Installer settings page + `POST /api/config`. Pure host-tested whitelist `components/webui/webui_settings.{h,c}` (write-whitelist EXCLUDES board_id/version/crc/provisioned/setup_password/webui_require_auth/relay state → settings can't weaken auth). Handler runs `guard_mutating` (auth+CSRF); body-only urlencoded; password never in URL; working-copy→sanitize→save→atomic swap→control_reload. GET emits installer field names (no secrets). Host tests PASS (ASan/UBSan). ESP-IDF build must run on the Mac. Awaiting Codex before bench/merge |
| SVC-015 | MQTT optional design | ChatGPT | Planned | Low | Not required for standalone |

---

## Sprint 4 — Multi-target HAL (KinCony first)

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-021 | HAL interface + audit + migration plan | Claude | Review | High | `components/svc_hal/include/hal.h` + skeletons + `docs/30_HAL_ARCHITECTURE.md`. Interface only, nothing wired. Key finding: KinCony IO = I2C PCF8574 expander -> mask-oriented HAL. **Codex-fix:** removed the stale duplicate proposed-v0 API from `hal.h` (was HAL_MAX_RELAY=16 + `hal_relay_get_count()`, conflicting with the implemented `hal_board.h` HAL_MAX_RELAY=32 + `hal_relay_count()`); `hal.h` now just `#include`s `hal_board.h` (single contract). Doc symbol table updated. **Codex-fix:** renamed local component from `hal` to `svc_hal` to avoid shadowing ESP-IDF internal `hal/...` headers. Awaiting CTO/Codex |
| SVC-022 | KinCony pinout/expander map | ChatGPT | Blocked | High | BLOCKING: need KC868-A8/A16/E16 schematics (PCF8574 addrs, relay/input/RS485 maps) |
| SVC-023 | hal_kincony impl (KC868-A8) | Claude | Review | High | SVC-CORE-001: PCF8574 pure map + I2C skeleton (bounded, fail-safe), KC868-A8/A16 profiles. RG-K1 open. Host-tested map/fault. Awaiting KinCony pinout (SVC-022) + bench |
| SVC-024 | Refactor IO services onto HAL | Claude | Planned | High | relay/dinput/rs485/button/indicator -> hal_* (policy kept) |
| SVC-025 | hal_svc100 (re-home board.c) | Claude | Planned | Medium | Phase 2; behavior parity |
| SVC-026 | Multi-target build + OTA board-id guard | Claude | In Progress | Medium | **Board-id guard DONE**: storage.c rejects wrong-board config -> factory default + stamps CONFIG_SVC_BOARD_ID (`apply_board_guard`). **Migration completed** v1/v2/v3/v4->v5 (fixed relay-array over-read; presence->sensor[0] map). 122 host tests PASS incl. schema-size distinctness. **Codex-fix:** HAL `Kconfig` default reverted to `SVC_TARGET_SVC100_REVA` (app_main still boots the SVC-100 board path directly; a KinCony default would stamp the wrong board_id → dangerous board-guard mismatch — do not default KinCony until SVC-024/025 wire app/services through `hal_install()`/`hal_board_init()`). **Codex-fix:** migration no longer forces `was_default=true` (v2/v3/v4 preserve provisioning; contract now: migrated≠default) — needs bench confirmation (IDF/NVS-bound). **CI build matrix DONE 2026-07-02**: `.github/workflows/build.yml` now = host-tests gate → `build` matrix (`idf.py set-target <t>` + build + size). Active leg: esp32s3/SVC-100. **Per-target sdkconfig split DONE**: `sdkconfig.defaults` (target-neutral) + `sdkconfig.defaults.esp32s3` (SVC-100: 16MB QIO, octal PSRAM, USB-Serial-JTAG, `SVC_TARGET_SVC100_REVA`) + `sdkconfig.defaults.esp32` (KinCony: 4MB DIO, no PSRAM, UART console, `SVC_TARGET_KINCONY_A8`) prepared but commented out of CI. Verified: common+esp32s3 union == prior single file (+ explicit board profile). TODO remaining: enable esp32 leg after HAL wiring (SVC-024/025) + 4MB `partitions_kincony.csv` |

---

## Sprint 3 — Hardware / PCB

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-016 | Hardware block diagram Rev A | ChatGPT | Planned | High | Power, MCU, IO |
| SVC-017 | BOM Rev A | ChatGPT | Planned | High | ESP32-S3, RS485, relay |
| SVC-018 | PCB schematic checklist | ChatGPT | Planned | High | Protection and strapping |
| SVC-019 | KiCad project structure | Claude | Planned | Medium | hardware/kicad |
| SVC-020 | Manufacturing checklist review | Codex | Planned | Medium | DRC/ERC/test fixture |

---

## Current Blockers

| Blocker | Owner | Resolution |
|---|---|---|
| Real mmWave sensor not yet received | Owner | Continue docs, firmware review, bench prep |
| ESP-IDF local build | Owner | RESOLVED 2026-07-02: `idf.py build` PASSES on the Mac (target esp32s3, xtensa-esp-elf 13.2.0, 1022/1022 steps, no errors). `svc-100.bin` ~458 KB, app partition 85% free. Host unit tests (`tests/host/run.sh`) also PASS. (CI sandbox still lacks idf.py — builds run on the workstation.) |
| Missing docs: 00_PROJECT_BIBLE, 10_SECURITY_STANDARD, 19_CODING_STANDARD | Owner/ChatGPT | `make doctor` flags these; create to satisfy mandatory reading order |
| GitHub Actions status not verified | ChatGPT | Check after `.github` commit |

---

## Next Command for Owner

```bash
make doctor
git status
```

Send the output to ChatGPT for CTO review.
