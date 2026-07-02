# Design Review DR-0002 — Firmware Sprint Review (SVC-FW-001…006)

Owner: Claude / Firmware Engineer
Scope: Build verification, board pin-map audit, and safety review of relay,
digital-input, RS485, and Modbus drivers while KiCad Rev A is in progress.
Source of truth: `hardware/schematic/*`, `docs/27_SCHEMATIC_DESIGN_REV_A.md`.
No hardware architecture changed. No GPIO changed.

Bench-test safety: the reviewed firmware paths are **safe for non-hazardous
bench testing**, subject to the open hardware release gates (RG-1, RG-5, RG-6).

---

## SVC-FW-001 — Build Verification

`make doctor` → **NEEDS ATTENTION (11/18)**, `make build` → **fail (idf.py not
found)**. The failures separate into two classes:

Real repository gaps (should be tracked):
- `docs/00_PROJECT_BIBLE.md`, `docs/10_SECURITY_STANDARD.md`,
  `docs/19_CODING_STANDARD.md` are missing — these are in the mandatory reading
  list and `doctor` checks for them. They block the documented "read first" flow.

Environment-only gaps (this CI sandbox, NOT repo defects):
- `idf.py`, `cmake`, `ninja`, `code` are not installed in the sandbox, so
  `make build` cannot run here. The ESP-IDF build must be executed on the
  developer Mac (toolchain being set up separately). Host unit tests
  (`tests/host/run.sh`) DO run here and pass (see Test Result).

No unrelated issues were fixed (per task instruction). The missing docs are
reported, not authored.

---

## SVC-FW-002 — Board Pin-Map Audit

**Blocker:** the named `hardware/pin_database/svc100_pin_database_rev_a.csv` did
not exist, and the schematic sheets assign **net names and functional intent but
no concrete GPIO numbers yet** (KiCad in progress). The only pin-level statement
in hardware is `02_ESP32_S3_CORE.md`: "Confirm GPIO15/16/17/47/48 availability
and boot behavior", and the firmware rule "All GPIO definitions must be
centralized in `components/board/include/board.h`".

Therefore there is **no authoritative counterpart to diff against**, so a true
mismatch report is not yet possible. As a remediation, this review derives
`hardware/pin_database/svc100_pin_database_rev_a.csv` **from `board.h`** as a
firmware-side seed, every flagged pin marked `PENDING-HW-CONFIRM`, for hardware
to confirm during KiCad. board.h was audited against ESP32-S3 strapping/boot/USB
constraints instead:

| Finding | Detail | Verdict |
|---|---|---|
| Flash/PSRAM pins avoided | board.h uses no GPIO33–37 (octal PSRAM on N16R8) and no GPIO26–32 (SPI flash) | PASS |
| USB native preserved | GPIO19/20 (USB D-/D+) not reused; console is USB-Serial-JTAG | PASS |
| Strapping pins clear | GPIO0/3/45/46 not used for board functions | PASS |
| Relay pins | GPIO4–7, none are strapping; driven LOW in `board_init` | PASS (fw) / see RG-6 (hw) |
| DIN GPIO15/16/17 | Boot/UART-adjacent; already flagged in board.h | OPEN — RG-1 |
| RS485-B GPIO47/48 | Schematic asks to confirm; 48 is WS2812 strap on some dev boards | OPEN — RG-5 |
| HMI GPIO39/40/41 | These are the pin-JTAG pins (MTCK/MTDO/MTDI); fine because USB-JTAG is used, but pin-JTAG debug is unavailable | NOTE |

No GPIO was changed. New gates RG-5, RG-6 opened (below).

---

## SVC-FW-003 — Relay Safety Review — PASS

Relay safe-off is now enforced in **three independent layers**:
1. `board_init` (`board.c:39`) drives every relay GPIO LOW before any logic runs.
2. `relay_apply_safe` (`relay.c:108`) applies the configured safe state at init
   and on failsafe, best-effort even if the mutex is contended.
3. `svc_config_sanitize` (`svc_config.c:100`) **forces `relay_safe_on[i] = 0`**,
   so a corrupt/forged NVS config can never define an energized "safe" state.

- Cannot energize before init: `relay_set` returns `ESP_ERR_INVALID_STATE`
  while `!s_initialized` (`relay.c:86`).
- Bounded mutex waits: `relay_set` and `relay_apply_safe` use
  `RELAY_LOCK_TIMEOUT_MS` (200 ms), never `portMAX_DELAY` (`relay.c:89,115`).
- Cleanup on init failure: if `relay_apply_safe` fails, `relay_init` deletes the
  mutex and clears `s_initialized` (`relay.c:70–78`).
- Test: `tests/host/test_main.c` asserts "relay safe state forced de-energized".

Residual (hardware): pre-firmware high-Z window on GPIO4–7 → RG-6.

---

## SVC-FW-004 — Digital Input Review — PASS

- Debounce: saturating integrator (`dinput_debounce.h`), host-tested.
- Dry-contact support: `read_active` honors the per-channel `active_low` mask
  with input pull-ups (`dinput.c:29`); closed contact to GND reads active.
- Eventbus dispatch: every accepted transition posts `EVT_DINPUT_EDGE`
  (`dinput.c:64–69`).
- Zero-tick impossible — **triple guarded**: `svc_config_sanitize` clamps
  `din_debounce_ms` to [1, 2000]; `dinput_start` clamps ticks to [1, 255]
  (`dinput.c:84`); `dinput_debounce_init` forces `max ≥ 1`
  (`dinput_debounce.h:29`). A corrupt config cannot create a 0-tick delay.
- Init cleanup: ISR handlers are unwound if a handler add or task create fails
  (`dinput.c:99–122`).

Low-priority note: `s_stable_mask` is `volatile uint8_t` (single writer); for
symmetry with `relay.c` it could be `atomic_uchar`, but the current use is safe.

---

## SVC-FW-005 — RS485 Driver Review — PASS

- Bounded transaction: mutex acquire is `timeout_ms + RS485_LOCK_EXTRA_MS`
  (`rs485.c:125`); `uart_wait_tx_done` and `uart_read_bytes` both use
  `timeout_ms` (`rs485.c:141,147`). No `portMAX_DELAY` in the production path.
- Init cleanup: `rs485_open` unwinds `uart_driver_delete` on any post-install
  failure via a `fail:` label (`rs485.c:104–108`).
- DE/RE timing: uses hardware `UART_MODE_RS485_HALF_DUPLEX` so DE is toggled by
  the UART around each frame (no software-timed GPIO race), and
  `uart_wait_tx_done` guarantees DE releases only after the last bit
  (`rs485.c:85,141`). Safe.

Note: the only `portMAX_DELAY` in the tree is the **opt-in** fully-blocking mode
of `eventbus_receive` (`eventbus.c:74`, used only when caller passes
`UINT32_MAX`). The production control task uses a bounded 250 ms tick
(`control.c:165`), so the control-critical path never blocks forever. Recommend
documenting that no production caller may pass `UINT32_MAX`.

---

## SVC-FW-006 — Modbus Master Review — PASS

- Length-before-payload: `mb_transact` rejects frames `< 5` bytes before any
  payload access (`modbus_master.c:28`); reads validate
  `rlen == 5 + count*2` before extracting registers (`:84`).
- CRC: validated in `mb_transact` before returning success (`:34–35`).
- Write echo: FC06 verifies `rlen == 8` plus slave, function, register address
  AND value echo (`:76`); FC16 verifies `rlen == 8` plus address and count echo.
- Short/malformed frames rejected as `SVC_ERR_CRC` (`:28`); exception frames must
  be exactly 5 bytes (`:71`).

Remaining risks (low): broadcast writes (slave 0) are rejected by an arg check
rather than supported (acceptable for this product); inter-frame T3.5 timing is
delegated to `RS485_RX_TOUT_SYMBOLS = 3` rather than computed from baud (fine at
9600–115200, revisit if very low baud is used).

---

## New Release Gates Discovered

- **RG-5** — Confirm RS485-B GPIO47/48 and DIN GPIO15/16/17 in the KiCad
  schematic; produce the authoritative pin database and reconcile with the
  firmware-derived seed.
- **RG-6** — Relay driver inputs (GPIO4–7) need an external pull-down so relays
  cannot momentarily energize during the power-on → `board_init` high-Z window.
- **RG-7** — `netmgr_enter_provisioning()` returns `SVC_ERR_NOT_IMPLEMENTED`
  (AP mode not built). Until implemented, provisioning works only via the
  physical config button at boot; do not advertise "AP provisioning" until the
  STA/LAN path is de-exposed first.

See `docs/RELEASE_GATES.md`.
