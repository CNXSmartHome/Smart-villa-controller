# Review Request — SVC-015 Phase 0 (MQTT config schema v6, config-only)

> **STATUS: CLOSED — Codex FINAL APPROVE (2026-07-03)** for config/schema/whitelist/
> tests. **Bench-safe.** P2 (mqtt_port wrap) and P3 (doc wording) resolved; all five
> design points confirmed. `idf.py build` PASS (.bin built), host tests ALL PASS.
> Phase 1 (telemetry publisher) and Phase 2 (gated command subscriber) remain out
> of scope / not started.

- **Date:** 2026-07-03
- **Author:** Claude (implementer)
- **Reviewer:** Codex
- **Spec:** `docs/40_MQTT_CNX_INTEGRATION.md` §4, §10 (Phase 0), §11, §12
- **Patch:** `svc015_phase0.patch` (in outputs)
- **Scope guard:** config/schema/whitelist/tests ONLY. **No** MQTT network
  connection, no publisher, no command subscriber (Phase 1/2). MQTT is **disabled
  by default** and remote control is **disabled by default**.

## Verification done here
- `bash tests/host/run.sh` → **ALL PASS** (ASan/UBSan), incl. new MQTT suite.
- Offline `gcc -std=c11 -D_GNU_SOURCE -fsyntax-only -Wall -Wextra` against the real
  project headers (ESP-IDF stubs): `storage.c` and `webui.c` exit 0.
  `svc_config.c` + `webui_settings.c` are host-compiled in the test build.
- Not done here: full `idf.py build` (idf.py absent in sandbox) — please build on
  the Mac to confirm link + .bin.

## Changes

| File | Change |
|------|--------|
| `storage/include/svc_config.h` | `SVC_CONFIG_VERSION` 5→**6**; MQTT `#define`s; MQTT block appended to `svc_config_t` (before `crc`); frozen `svc_config_v5_t` for migration. |
| `storage/svc_config.c` | MQTT defaults (off / port 1883 / timeout 5000 / remote-control off); sanitizer: NUL-terminate strings, normalize booleans, default port/timeout, build empty topic prefix `svc/<board>/<name>` and scrub to `[a-zA-Z0-9_-/]`. |
| `storage/storage.c` | `config_migrate` **v5→v6** (field-wise copy of the shared prefix; MQTT stays at defaults). |
| `webui/webui_settings.c/.h` | POST whitelist += MQTT installer fields; `webui_settings_apply` handles them; new `webui_settings_is_secret()` (`wifi_pass`,`setup_password`,`mqtt_pass`). |
| `webui/webui.c` | `h_config_get` comment: secrets incl. `mqtt_pass` never emitted (handler emits an explicit non-secret list; no MQTT field in Phase 0). |
| `tests/host/test_main.c` | schema-size distinctness v1..**v6**; MQTT whitelist/secret/apply asserts; new `test_mqtt_config()` (defaults, sanitize clamps, prefix build+scrub, NUL-term). |

## Key design decisions (please confirm)

1. **Migration is field-wise, not a prefix `memcpy`.** v6 = v5 + MQTT block *before*
   `crc`; v5's tail padding before `crc` differs from v6's MQTT bytes, so a
   `memcpy(out, raw, offsetof(v5,crc))` would corrupt the MQTT defaults. Copied
   each shared field explicitly instead. `config_crc` uses `offsetof(...,crc)` so
   it now covers the MQTT block automatically.
2. **Frozen `svc_config_v5_t`** mirrors the previous `svc_config_t` exactly (verify
   field-for-field). Distinctness test guarantees v5≠v6 so `config_migrate` picks
   the right branch.
3. **Topic-prefix charset** allows `/` (hierarchy separator) plus `[A-Za-z0-9_-]`;
   MQTT wildcards `#`/`+` and spaces are scrubbed to `_`. Spec says `[a-zA-Z0-9_-]`
   for the substituted parts — confirm allowing `/` as separator is acceptable.
4. **`mqtt_allow_remote_control` is installer-writable** (whitelist) but defaults 0
   and is inert in Phase 0 (no subscriber). Confirm it belongs in the whitelist now
   vs. deferring to Phase 2.
5. **`mqtt_pass` write-only:** writable via POST body, in `SECRET_KEYS`, and GET
   emits no MQTT field. Confirm this satisfies "never returned by GET".

## Round 2 — IDF build fix (`-Werror=format-truncation`)

The Mac `idf.py build` (`-O2 -Werror=all`) flagged the topic-prefix `snprintf`:
`svc/%s/%s` with `board_id`(≤31) + `device_name`(≤31) can exceed the 64-byte
buffer (bounded by `snprintf`, but rejected by `-Werror=format-truncation`). My
offline `-fsyntax-only` didn't exercise `-O2`, so it slipped through.

**FIX:** capped each field — `"svc/%.29s/%.29s"` — so the worst case is
`4 + 29 + 1 + 29 = 63 (+NUL) = 64`, guaranteed to fit; installers can still set a
full prefix. Re-verified with `gcc -O2 -Werror=format-truncation -Werror=format-overflow`
on `svc_config.c`/`storage.c`/`webui.c`/`webui_settings.c` → all exit 0; host tests
ALL PASS. (Offline check upgraded to mirror IDF's strict flags going forward.)

Drive-by (unrelated to SVC-015): fixed the build NOTICE by renaming
`CONFIG_APP_ANTI_ROLLBACK` → `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` in
`sdkconfig.defaults` (IDF 5.2 rename).

## Round 3 — Codex P2/P3 fixed

Codex confirmed the design (v5 layout match, field-wise migration, topic-prefix
charset, whitelist stance, mqtt_pass write-only) and raised two items:

- **P2 (mqtt_port wrap):** `(uint16_t)to_u32(value)` let `70000` wrap to `4464`,
  a valid-looking port the sanitizer would accept. **FIX:** parse in `uint32_t`;
  if `p == 0 || p > 65535` store `0` so `svc_config_sanitize` defaults it to
  `SVC_MQTT_PORT_DEFAULT` (1883). New host tests: `70000` → apply `0` → sanitize
  `1883`.
- **P3 (misleading doc):** `webui_settings_apply`'s comment called rejected keys
  "protected/secret", but secrets (`wifi_pass`, `mqtt_pass`) are intentionally
  writable write-only. **FIX:** reworded to "unknown or non-whitelisted
  (protected) field" + a note that the whitelist deliberately includes write-only
  secrets (never returned by GET).

Verify: `bash tests/host/run.sh` → ALL PASS (incl. the `70000` case); strict
offline compile (`-O2 -Werror=format-truncation`) of `webui_settings.c` → exit 0.
No other files changed this round (`webui_settings.c`, `webui_settings.h`,
`test_main.c`).

## Spec conformance (Phase 0 + §11/§12)
- [x] schema v6 fields + v5→v6 migration preserving provisioning/fields.
- [x] sanitizer clamps port/booleans, NUL-terminates, defaults prefix.
- [x] `mqtt_pass` write-only, not returned by GET.
- [x] Web UI whitelist adds MQTT fields; still excludes setup_password/provisioned/
      webui_require_auth/board identity/relay polarity+safe-state.
- [x] MQTT disabled by default; remote control disabled by default.
- [x] No passwords in URL (POST body only); mutating API stays auth+CSRF.
- [ ] (Phase 1/2, NOT in scope) telemetry publisher, command parser/gate, ACK.
