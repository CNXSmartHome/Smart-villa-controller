# 30 — Hardware Abstraction Layer (HAL) Architecture & Migration Plan

Direction update: Smart Villa OS must run on **multiple hardware targets** from
one core firmware. KinCony first (MVP/demo), SVC-100 Rev A later. Existing
SVC-100 work is **kept**, not discarded. This document is the audit + design +
plan. No existing module is rewritten yet (per the task).

Targets: **KinCony KC868-A8, KC868-A16, E16 (ESP32)** now; **SVC-100 Pro Rev A
(ESP32-S3)** later.

```
app logic  ->  logic engine  ->  io service  ->  [ hal.h ]  ->  hal_<target>
                                                                ├ hal_kincony
                                                                └ hal_svc100
```

## A. Audit — current hardware coupling

Grep-based audit of `components/`:

| Component | HW coupling found | Verdict |
|-----------|-------------------|---------|
| `board` | pin map + `gpio_config`, `i2c_new_master_bus`, `board_*_gpio` | **becomes hal_svc100** |
| `relay` | `gpio_set_level(board_relay_gpio())`, `BOARD_RELAY_COUNT` | refactor → call `hal_relay_*` |
| `dinput` | `gpio_get_level(board_dinput_gpio())`, ISR, `BOARD_DINPUT_COUNT` | refactor → poll `hal_din_read_mask` |
| `rs485` | `uart_*`, `BOARD_RS485*` pins/port | refactor → `hal_rs485_map` for pins; UART transport stays |
| `button` | `gpio_*`, board button pins | refactor → `hal_button_read_mask` |
| `indicator` | `led_strip` (RMT), `ledc` (PWM) | refactor → `hal_led_set`, `hal_buzzer_set` |
| `modbus_master` | none (transport via rs485) | **common, unchanged** |
| `presence` | none (uses modbus) | **common, unchanged** |
| `logic`, `control_logic` | none (counts passed as params) | **common, unchanged** |
| `control` | `BOARD_RELAY_COUNT/DINPUT_COUNT` only | minor: use `hal_board()` counts |
| `storage`/`svc_config` | `BOARD_RELAY_COUNT/DINPUT_COUNT` size arrays | schema sizing → `HAL_MAX_*` (Phase 3) |
| `webui`, `netmgr`, `health`, `eventbus`, `svc_common` | none board-specific | **common** |

**Key structural finding:** KinCony drives relays and reads inputs through **I2C
PCF8574 expanders**, while SVC-100 uses native GPIO. So the abstraction boundary
must be *mask-oriented IO primitives* (`set relay N`, `read input mask`), NOT GPIO
numbers. Today's `board_relay_gpio()` model only fits native-GPIO boards.

## 1. Files that need refactor

Refactor (move HW bodies behind `hal_*`, keep all policy/logic):
- `components/relay/relay.c` — keep safe-state, atomic mask, bounded locks; replace
  `gpio_set_level(board_relay_gpio(...))` with `hal_relay_write_mask/_write`.
- `components/dinput/dinput.c` — keep debounce integrator + eventbus; replace
  per-pin `gpio_get_level` with a periodic `hal_din_read_mask()` (drop the GPIO
  ISR; expander backends can't ISR per pin anyway).
- `components/rs485/rs485.c` — keep half-duplex transport; get UART/pins from
  `hal_rs485_map()` instead of `board.h`.
- `components/button/button.c` — replace GPIO reads with `hal_button_read_mask()`.
- `components/indicator/indicator.c` — replace `led_strip`/`ledc` with
  `hal_led_set()`/`hal_buzzer_set()`.
- `components/board/*` — re-home into `hal_svc100` (native impl).
- Light touch: `control.c`, `svc_config.*` — counts via `hal_board()` / `HAL_MAX_*`.

Unchanged (already hardware-independent): logic, control_logic, modbus_master,
presence, webui, netmgr, health, eventbus, svc_common, storage(core).

## A2. Proposed folder structure

The requested logical layout maps onto the existing ESP-IDF `components/` tree as
follows (no existing component is physically moved yet — this is the target):

| Requested group | Holds | Current components |
|-----------------|-------|--------------------|
| `core/` | platform, types, bus, health, persisted config | svc_common, eventbus, health, storage |
| `services/` | IO + protocol services | relay, dinput, rs485, button, indicator, modbus_master, presence, netmgr |
| `logic/` | rule engine + wiring | logic, control_logic, control |
| `api/` | local web UI + REST | webui |
| `security/` | authz, sanitize (cross-cutting) | webui_authz, svc_config sanitize |
| `ota/` | image validation + rollback | (app_main + esp_https_ota) |
| `hal/` | **board abstraction (NEW)** | `components/svc_hal` contract + `components/hal_svc100`, `components/hal_kincony` implementations |

The new HAL is concrete now:

```
components/svc_hal/
  include/hal.h     # the contract (always compiled/exported)
components/hal_svc100/
  hal_svc100.c      # Phase 2: ESP32-S3 native GPIO/RMT/LEDC impl
components/hal_kincony/
  hal_kincony.c     # Phase 1: ESP32 + PCF8574 I2C-expander impl
```

The common component is named `svc_hal` to avoid shadowing ESP-IDF's internal
`hal/...` headers. One `hal_<target>` component is compiled per build via
`CONFIG_SVC_TARGET_*`. The `core/services/logic/api/...` grouping is
documentation-level for now; a physical reorg into `app/` sub-folders is an
optional later cleanup (not required for multi-target and risky to do early).

## 2. Proposed HAL API

The HAL contract is defined ONCE in `components/svc_hal/include/hal_board.h` (the
implemented, host-tested dispatcher). `hal.h` is a thin compatibility redirect
that just includes `hal_board.h`, so there is a single source of truth — no
duplicate maxima or symbol names. New code should include `hal_board.h`.
Functions are `hal_`-prefixed to avoid colliding with the existing, retained
`relay_set`/`relay_get` service functions (renaming those is a later phase, not a
rewrite now). Requested interface → implemented symbol:

| Requested | Implemented symbol (hal_board.h) |
|-----------|----------------------------------|
| `relay_get_count()` | `hal_relay_count()` |
| `relay_set()` | `hal_relay_set()` |
| `relay_get()` | `hal_relay_get()` |
| `di_get_count()` | `hal_din_count()` |
| `di_read()` | `hal_din_read()` |
| `rs485_get_port()` | `hal_rs485_get_config()` |
| `board_get_info()` | `hal_board_get_info()` |
| `board_apply_safe_state()` | `hal_board_apply_safe_state()` |

Plus, for expander efficiency / failsafe, `hal_board.h` implements today:
`hal_relay_set_mask()`, `hal_relay_get_mask()`, `hal_din_read_mask()`, and
`hal_board_init()`. (Button/LED/buzzer/I2C-bus primitives are future additions to
the same contract, not yet in `hal_board.h`.) Summary:
- `hal_init()`, `hal_board()` → `hal_board_info_t` (board_id, relay/din/rs485/
  button/led counts, `caps` bits incl. `HAL_CAP_IO_EXPANDER`, `HAL_CAP_RGB_LED`).
- Relays: `hal_relay_write(ch,on)`, `hal_relay_write_mask(mask)`,
  `hal_relay_get_mask()` — mask-first so expander backends apply all outputs in
  one atomic I2C write (also the failsafe path).
- Inputs: `hal_din_read_mask()` (batch, expander-friendly), `hal_din_read(ch)`.
- Buttons: `hal_button_read_mask()`.
- Indicator: `hal_led_set(idx,r,g,b)`, `hal_buzzer_set(on,freq)`.
- RS485: `hal_rs485_map(bus_id,&map)` (UART + pins); transport stays common.
- I2C: `hal_i2c_bus()` (opaque handle for RTC + expanders).
- `HAL_MAX_RELAY=16`, `HAL_MAX_DIN=16` set shared sizing (KC868-A16 ceiling).

Selection is **build-time** (`CONFIG_SVC_TARGET_*`), one `hal_<target>` linked —
no runtime vtable overhead, standard ESP-IDF idiom.

## 3. KinCony target requirements (hal_kincony)

- **External inputs needed:** KC868-A8/A16/E16 schematics — PCF8574 I2C
  addresses, relay-bit↔expander mapping, input-bit↔expander mapping, RS485 UART
  + DE pin, button/LED pins. (Blocking dependency — listed in Risks.)
- ESP32 (classic) IDF target: `idf.py set-target esp32`. No native USB → console
  over UART bridge (CP2102/CH340).
- IO over I2C: `hal_relay_write_mask` = one/two PCF8574 writes; `hal_din_read_mask`
  = PCF8574 read(s). Must be bounded + handle I2C errors (see Risks).
- **Safe-off on KinCony:** confirm relay drive polarity so the PCF8574 power-on /
  reset default (outputs = 1) maps to **relay de-energized**; else add an init
  that forces the safe state and a watchdog-backed re-assert. (KinCony analogue of
  RG-6.)
- Caps: `HAL_CAP_IO_EXPANDER`; usually no WS2812 RGB (simple LEDs); buzzer varies.
- A8 = 8/8, A16 = 16/16 → exercises the dynamic-count path first.

## 4. SVC-100 target requirements (hal_svc100)

- Re-wrap existing `board.c` + the GPIO bodies already in relay/dinput/rs485/
  button/indicator — **no logic change**, just relocate behind `hal_*`.
- ESP32-S3, native GPIO; counts 4 relays / 8 inputs / 2 RS485; WS2812 RGB
  (`HAL_CAP_RGB_LED`), LEDC buzzer, native USB-Serial-JTAG, I2C RTC.
- Pin map from `hardware/pin_database/svc100_pin_database_rev_a.csv` + `board.h`
  (unchanged). Honors the open release gates (RG-1/5/6).
- Goal: byte-for-byte behavior parity with today's SVC-100 firmware.

## 5. Migration steps

**Phase 0 — scaffold (this change):** `components/svc_hal/include/hal.h` interface +
`hal_kincony`/`hal_svc100` skeletons + this plan. Nothing wired; build unchanged.

**Phase 1 — KinCony MVP:**
1. Add Kconfig `CONFIG_SVC_TARGET_*`; default KINCONY_A8.
2. Implement `hal_kincony` (PCF8574 relay/input, RS485 map, buttons/LED, board
   info). Host-test the pure parts (bit mapping, mask packing).
3. Refactor relay/dinput/rs485/button/indicator to call `hal_*` (policy kept).
   dinput switches to periodic batch read (drop GPIO ISR).
4. Bring up logic engine + web UI + API + MQTT + OTA + presence on KC868-A8.
5. Validate safe-off, I2C-fault failsafe, and OTA on real KinCony hardware.

**Phase 2 — SVC-100 target:**
1. Implement `hal_svc100` by re-homing `board.c` + the existing GPIO bodies.
2. Build `CONFIG_SVC_TARGET_SVC100_REVA`; verify behavior parity + host tests.
3. Retire direct `board.h` includes outside `hal_svc100`.

**Phase 3 — shared release build system:**
1. Per-target `sdkconfig.defaults.<target>`, partition tables, IDF target
   (esp32 vs esp32s3); CI build matrix for all targets.
2. **OTA safety:** embed `board_id` in the app descriptor; OTA rejects an image
   whose `board_id` ≠ running board (no cross-flashing KinCony↔SVC-100).
3. Unify config schema to `HAL_MAX_*` sizing + migration; web UI renders relays/
   inputs dynamically from `hal_board()` counts.
4. Artifact naming `svc-os-<board_id>-<ver>.bin`; release notes per target.

## 6. Risks

- **I2C-expander safe-off (SAFETY, high):** native-GPIO safe-off is deterministic;
  KinCony relay safe-off depends on a healthy I2C bus. A hung bus could strand
  relays energized. Mitigation: confirm PCF8574 power-on default = de-energized,
  bounded I2C with ret/timeout, watchdog + expander re-init, periodic safe-state
  re-assert.
- **Missing KinCony pinout (blocking):** need official KC868-A8/A16/E16
  schematics/expander maps before `hal_kincony` can be real. Until then it is a
  documented skeleton.
- **Two MCU families:** ESP32 (KinCony) vs ESP32-S3 (SVC-100) — different IDF
  target, USB/console, strapping. Build system must carry both.
- **Channel-count divergence (4/8 vs 16/16):** config schema + web UI must be
  dynamic; a v4 schema + migration is needed (Phase 3) and must not regress the
  v3 work just finished.
- **dinput model change:** dropping the GPIO ISR for batch polling changes edge
  latency/debounce timing; re-validate against the existing host debounce tests
  and bench.
- **Indicator capability gaps:** KinCony may lack RGB/buzzer; HAL must degrade via
  `caps` without breaking the common indicator policy.
- **Regression risk to shipped work:** the security, logic-engine, and OTA-health
  work must remain target-independent and keep passing host tests through the
  refactor (run `tests/host/run.sh` at each step).
- **Scope creep:** keep Phase 1 to KC868-A8 only; A16/E16 after the pattern is
  proven.
