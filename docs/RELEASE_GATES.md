# SVC-100 Release Gates

Hardware/firmware checks that MUST pass before a production release or PCB tape-out.

## RG-1 — ESP32-S3 GPIO15 / GPIO16 / GPIO17 boot & JTAG review

**Status: OPEN — schematic review required.**

In the V1 reference pin map (`components/board/include/board.h`) digital inputs
DIN_0/1/2 are assigned to **GPIO15, GPIO16, GPIO17**. On ESP32-S3 these pins have
secondary roles that interact with reset/boot and debug:

- GPIO15/16 are routed near the USB-Serial-JTAG / UART0 bring-up path on many
  WROOM-1 reference designs.
- A front-end that forces any of these LOW (or to an unexpected level) during
  reset can disturb boot-mode selection or contend with USB-JTAG enumeration,
  producing intermittent "won't boot / won't flash" units that pass bench test
  but fail in the field.

**Required actions before release:**

1. Confirm on the schematic that each opto-isolator output presents a **defined,
   benign level at power-on reset** (pulled to the non-asserting state, matching
   the internal pull-ups configured in `board_init()`).
2. Confirm there is **no contention** with USB-Serial-JTAG (GPIO19/20) or the
   programming UART during reset and download mode.
3. Verify download mode and normal boot with **all 8 inputs both open and
   asserted** (especially DIN_0..2 held active during reset).
4. If any check fails, **reassign DIN_0..2** to spare GPIOs — inputs are the
   lowest-risk signals to move, and the change is isolated to `board.h`.

Sign-off: ______________________  Date: __________

## RG-2 — Relay safe-state at brown-out / reset

Verify that relays remain in (or return to) the configured safe state across:
power-on, watchdog reset, brown-out, and OTA reboot. Covered in firmware by
`board_init()` + `relay_apply_safe()`, but must be confirmed on hardware with a
scope on the coil drivers.

## RG-3 — OTA rollback end-to-end

Flash a deliberately-faulty image and confirm the health gate
(`validate_image_when_healthy()`) leaves it unconfirmed and the bootloader rolls
back to the previous slot on the next boot.

## RG-4 — Provisioning lockout

Confirm a factory-fresh (un-provisioned) unit rejects every mutating API route
(`/api/io`, `/api/ota`) with 403 until a setup password is established, and that
relay control is impossible without auth + CSRF.
