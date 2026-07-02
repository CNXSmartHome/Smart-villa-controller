# Design Review DR-0003 — Logic Engine Integration & Schema v3 (SVC-013 / SVC-012)

Owner: Claude / Firmware Engineer. Status: **Review** (gated; not production-on).
Bench-test safety: safe — the new path is compiled out by default
(`CONFIG_SVC_USE_LOGIC_ENGINE = n`); production behavior is unchanged.

## Scope

1. Config schema **v3**: add `for_ms` (FOR dwell) and an explicit `action`
   (ON / OFF interlock) to `svc_rule_t`; add dry-contact fallback fields.
2. A pure, host-tested **adapter** from `svc_config_t` rules to the logic engine.
3. **Review-gated integration** into `control.c` behind a default-off Kconfig.
4. **SVC-012** dry-contact presence fallback.

## Schema v3 (`svc_config.h` / `svc_config.c` / `storage.c`)

- `svc_rule_t` gains `for_ms` (uint16, dwell), `action` (`SVC_RULE_ACTION_ON/OFF`),
  and a reserved byte. `on_active` is retained and selects the trigger polarity.
- New `fallback_din_enabled` / `fallback_din_chan` (SVC-012).
- The old rule layout is frozen as `svc_rule_legacy_t`; `svc_config_v1_t` now uses
  it, and a new `svc_config_v2_t` is added so both v1→v3 and v2→v3 migrate by
  intentional per-field copy. **Critical fix:** the old v1 migration did
  `memcpy(out->rule, v1->rule, sizeof(out->rule))`, which would over-read now
  that the v3 rule is larger — replaced with a per-rule `migrate_rule()` mapper.
- `svc_config_sanitize` clamps `for_ms` to `[0, 60000]`, normalizes `action`,
  and disables fallback if the channel is out of range. Relay safe-off remains
  forced de-energized.
- v2→v3 **preserves** provisioning + password (unlike v1→v3 which re-provisions).

## Adapter (`components/control/control_logic.{h,c}` — pure, host-tested)

- `svc_rule_to_logic()` — `trigger_src`→presence/dinput, `on_active`→cond
  (ACTIVE/INACTIVE), `action`→ON/OFF, `for_ms`/`off_delay_s` carry through.
- `logic_ruleset_from_config()` — builds and **validates** the ruleset (fail
  closed at the engine boundary even though config is already sanitized).
- `presence_effective()` — SVC-012: RS485 is authoritative while fresh; when
  stale, an enabled dry-contact channel (mmWave S1/S2) becomes presence
  (asserted→PRESENT, clear→ABSENT); otherwise UNKNOWN.

## control.c integration (gated by `CONFIG_SVC_USE_LOGIC_ENGINE`, default n)

- When enabled: `evaluate_all_rules()` rebuilds the engine ruleset on config
  reload, computes effective presence (with fallback), evaluates the engine at
  `svc_now_ms()`, and drives each relay to the desired mask. The engine owns
  dwell/linger, so the legacy `service_off_delays()` and `s_off_deadline` are
  compiled out on this path.
- `EVT_PRESENCE_STALE`: with fallback enabled, evaluate via the dry contact
  instead of a blanket failsafe; with no fallback, `relay_apply_safe()` as before.
- When disabled (default): the legacy per-rule evaluator is unchanged.

## Tests

`tests/host/run.sh` — 76 assertions, ALL PASS, warning-clean under ASan/UBSan,
including: adapter field mapping (presence/dinput, ON/OFF, polarity), ruleset
validation, and the six `presence_effective` fallback cases (fresh authoritative,
stale→DI present/absent, no-fallback→UNKNOWN, bad-channel→UNKNOWN). Logic-engine
safety regressions (OFF-cancels-linger, unvalidated rule fail-closed, ms
wrap-around) remain green.

## Remaining before production enable (gate to flip the flag)

- Codex review of this integration and the v3 migration.
- On-target bench: v1→v3 and v2→v3 NVS migration on real flash; relay behavior
  parity vs legacy; dry-contact fallback timing with a real/forced-stale sensor.
- Web UI / REST support for the new rule fields (`for_ms`, `action`) and the
  fallback config (Gemini / SVC-014).
- Decide default rule semantics once both engines are field-validated; then make
  the logic engine the default and retire the legacy evaluator.
