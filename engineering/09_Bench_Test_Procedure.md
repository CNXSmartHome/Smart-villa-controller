# 09 — Bench Test Procedure (SVC-010)

Gate procedure to validate the logic-engine integration (SVC-013), config schema
v3, and the dry-contact fallback (SVC-012) **before** `CONFIG_SVC_USE_LOGIC_ENGINE`
may be flipped to default in a production build.

Drafted by Claude for Codex / CTO execution and sign-off. Subordinate to
`docs/00_PROJECT_BIBLE.md`. References: DR-0003, `docs/RELEASE_GATES.md`.

> SAFETY: bench only. Use **non-hazardous loads** — the status LED, the buzzer,
> or a low-voltage indicator lamp on each relay. Do NOT connect mains, pool
> pumps, or any hazardous load during bench validation. Keep the relay-driver
> external pull-down (RG-6) populated; if the prototype lacks it, probe the coil
> driver and treat any cold-boot relay chatter as a hard fail.

## 0. Preconditions

- [ ] Host unit tests pass: `tests/host/run.sh` → `ALL PASS`, exit 0 (76+ asserts).
- [ ] RG-1/RG-5/RG-6 schematic review status recorded (may still be OPEN; note it).
- [ ] A USB-C console attached; serial log captured for every step.
- [ ] Two known sensor states available: a real mmWave on RS485-A **or** a
      Modbus slave simulator, and a dry-contact switch on a chosen DI channel.

## 1. Build & flash matrix

Build two images from the same commit:

| ID | Config | Command |
|----|--------|---------|
| A (control) | flag OFF (legacy, production default) | `idf.py build` |
| B (under test) | flag ON | `idf.py menuconfig` → *SVC-100 Control* → enable *Use the IF/FOR/THEN logic engine*, then `idf.py build` |

- [ ] Both A and B build clean for `esp32s3` (no warnings in the changed components).
- [ ] Record `idf.py size-components` delta for the `logic` + `control` components.

## 2. Boot safety — relay safe-off (RG-2, RG-6)

For **both** A and B, and for cold power-on, EN reset, and OTA reboot:

- [ ] Relays read **de-energized** within the first board_init window; scope the
      coil drivers — no energize pulse during the power-on → firmware high-Z window.
- [ ] Serial shows `relay driver ready` and `safe state applied (mask=0x00)`.
- [ ] Factory-fresh unit boots un-provisioned; status LED shows boot→ok pattern.

## 3. Config migration on real flash (schema v3)

Pre-load NVS with older blobs (use a v1 and a v2 image, or a saved dump):

- [ ] **v1 → v3**: device boots, log shows `migrated config v1 -> v3 (re-provisioning required)`; device is **un-provisioned** afterward (security reset is intended).
- [ ] **v2 → v3**: log shows `migrated config v2 -> v3`; provisioning + setup
      password are **preserved** (still provisioned, login still works).
- [ ] **Corrupt CRC**: a tampered blob logs `config CRC bad; using defaults` and
      comes up safe (un-provisioned, relays safe-off).
- [ ] After each: `/api/config` reflects sane values; `for_ms`/`action` default
      to 0/ON for migrated rules; fallback disabled by default.

## 4. Functional parity — legacy (A) vs logic (B)

Configure one simple rule: presence PRESENT → relay 0 ON, 5 s off-delay.

- [ ] On A: presence present → relay0 ON; presence clears → relay0 OFF after ~5 s.
- [ ] On B: identical observable behavior (timing within tolerance).
- [ ] Any divergence is documented with the rule, inputs, and timings.

## 5. Logic-engine features (B only)

- [ ] **FOR dwell**: rule presence PRESENT FOR 2 s → relay0 ON. A <2 s blip does
      NOT energize; a sustained ≥2 s presence does.
- [ ] **OFF-delay linger**: after presence clears, relay holds ON for the
      configured off-delay, then releases.
- [ ] **OFF interlock (de-energize wins)**: add rule DI3 active → relay0 OFF.
      Assert DI3 while presence is ON → relay0 goes OFF immediately.
- [ ] **Interlock cancels linger**: presence ON → start off-delay (clear
      presence) → assert DI3 (relay OFF) → release DI3 **before** the off-delay
      would have expired → relay0 **stays OFF** (no re-energize). [DR-0003 fix]

## 6. SVC-012 dry-contact fallback (B only)

Enable fallback on a DI channel wired to the mmWave S1/S2 dry contact.

- [ ] RS485 sensor **fresh**: presence follows RS485 (dry contact ignored).
- [ ] RS485 sensor forced **stale** (disconnect/halt the slave): after the stale
      timeout, log shows `presence stale -> dry-contact fallback`; presence now
      follows the dry contact (assert → relay per rule, clear → release).
- [ ] RS485 recovers: presence returns to RS485 authority.

## 7. Failsafe on stale with NO fallback (B only)

- [ ] Disable fallback. Force RS485 stale → log `presence stale, no fallback ->
      failsafe`; relays driven to safe-off; status LED fault pattern.

## 8. OTA health gate + rollback (RG-3)

- [ ] A healthy OTA image is marked valid only after the health gate passes
      (control alive, presence ran/degraded, relay safe applied, net settled,
      watchdog stable). Confirm via `/api/status` `health` and the validate log.
- [ ] Flash a deliberately-faulty image → it is left **unconfirmed** and the
      bootloader rolls back on next boot.

## 9. Provisioning & auth lockout (RG-4)

- [ ] Factory-fresh: `POST /api/io` and `POST /api/ota` → 403 (not provisioned);
      `GET /api/io` → 401.
- [ ] Provisioning only via config-button-at-boot (or AP mode when implemented);
      ambient-LAN `POST /api/provision` → 403.
- [ ] After provisioning: mutating routes require `X-Auth-Token` + `X-SVC-CSRF`;
      missing either → 401/403. Relay control impossible without both.

## 10. Watchdog / no-blocking (both)

- [ ] Run B for ≥1 h with presence/DI activity; no Task Watchdog resets, no
      `bus mutex timeout` storms, no relay hang. `events_dropped` in `/api/status`
      stays 0 (or explained).
- [ ] Yank RS485 mid-transaction repeatedly → control loop stays responsive
      (bounded timeouts), relays follow failsafe/fallback as configured.

## 11. Decision gate (to flip the flag)

All of §2–§10 PASS on hardware, **and**:

- [ ] Codex review of DR-0003 + the v3 migration signed off.
- [ ] RG-2 and RG-6 closed (relay safe-off verified electrically).
- [ ] No open High/Critical findings against `control.c` / `control_logic.c`.

Only then may `CONFIG_SVC_USE_LOGIC_ENGINE` default to `y` (separate, reviewed
commit), with the legacy evaluator retained for one release as a fallback.

## Sign-off

| Role | Name | Result (PASS/FAIL) | Date | Notes |
|------|------|--------------------|------|-------|
| Firmware (Claude) | | | | drafted procedure |
| Reviewer (Codex) | | | | |
| CTO (ChatGPT) | | | | flag-flip approval |
