# 10 — Security Standard

Mandatory security requirements for SVC-100 firmware. Subordinate to
`docs/00_PROJECT_BIBLE.md`. Reviewed by Codex. Most items here are already
implemented; this document is the specification they must continue to satisfy.

## 1. Authentication

- Authentication is **mandatory and cannot be disabled by configuration**. The
  `webui_require_auth` flag is ignored by the mutating-route guard, and
  `svc_config_sanitize` forces it to 1 once the device is provisioned.
- A device is "provisioned" only when `provisioned == 1` **and** a non-empty
  `setup_password` exists. A provisioned flag with an empty password is collapsed
  to un-provisioned by the sanitizer.
- Every protected request must present `X-Auth-Token` equal to the setup
  password. Token comparison is length-independent (no early-exit timing leak).

### Protected routes

All mutating/control routes require authentication after provisioning:
`POST /api/io` (relay), `POST /api/config` (settings write), OTA, reboot, and
**`GET /api/io`** (reading physical I/O state is privileged). Privileged READS
require auth too: **`GET /api/config`** exposes installer config (SSID, enabled
interfaces, sensor topology, input polarity, provisioning state) and so uses the
same `webui_io_get_allowed(provisioned, has_auth)` gate as `GET /api/io` — it
returns 401 unless provisioned AND authenticated. It never returns secrets
(`wifi_pass`/`setup_password`), but it is not public. Reference predicates live in
`components/webui/webui_authz.c` and are unit-tested.

All JSON responses are built with bounded writes: single-shot responses check the
`snprintf`/append return, and multi-segment responses (`GET /api/io` labels) use
`buf_appendf`, which saturates the write position on truncation and never computes
an out-of-bounds pointer or an underflowing remaining-size. A response that would
overflow its buffer is returned as HTTP 500, never as silently-truncated JSON.

### Settings write — `POST /api/config` (SVC-014)

- Body is `application/x-www-form-urlencoded`; the handler runs `guard_mutating`
  (auth + CSRF) before parsing, so an unauthenticated caller cannot mutate config.
- Writes are restricted to an explicit **whitelist** (`webui_settings.c`
  `WRITABLE_KEYS`). Any non-listed key is rejected fail-closed. The whitelist
  deliberately **excludes** `board_id`, `version`, `crc`, `provisioned`,
  `setup_password`, `webui_require_auth`, and relay polarity/safe-state — a
  settings write can therefore never weaken auth or store an unsafe relay state.
- `wifi_pass` is accepted **only** as a normal body field, never a URL/query
  parameter (same rule as the setup password).
- Changes are applied to a working copy, clamped by `svc_config_sanitize`, and
  only persisted (then `control_reload_config`) on a successful `storage_save`;
  the live `s_cfg` is swapped atomically after save. Response reports
  `{"applied":N,"rejected":M}`.
- `webui_settings_is_writable` / `webui_settings_apply` are pure and
  host-unit-tested (rejection of protected fields leaves cfg unchanged).

## 2. Provisioning

- `POST /api/provision` is accepted **only** while provisioning mode is active:
  physical config button held at boot, **or** AP provisioning mode. There is **no
  ambient-LAN time window**.
- The setup password is read from the **POST body only**, never the URL/query
  string. Minimum length 8.
- Browser-originated provisioning requires the per-boot CSRF header.
- Until AP provisioning is implemented (`netmgr_enter_provisioning` returns
  `NOT_IMPLEMENTED`, see RG-7), the only working path is the config button at
  boot.

## 3. CSRF

- Every browser-originated mutating request must carry `X-SVC-CSRF` equal to the
  per-boot random token. Because it is a non-standard request header, a
  cross-site form/script cannot set it without a CORS preflight the server never
  approves.
- The token is served by `GET /api/csrf` to an authenticated client, or while in
  provisioning mode (so the setup page can obtain one before a password exists).
  Same-origin policy keeps it confidential to the device's own UI.

## 4. JSON / output safety

- All JSON strings derived from config or the network must be escaped with the
  bounded `svc_json_escape_n(src, src_len, dst, dst_len)`.
- Fixed-size config arrays must be escaped using `strnlen(arr, sizeof(arr))` as
  the length — never `strlen` on a possibly-unterminated array. The escaper never
  reads past `src_len`.
- Secrets (`wifi_pass`, `setup_password`) are never returned by any API.

## 5. Configuration integrity

- Configuration is a versioned, CRC-protected NVS blob. After the CRC check, and
  after any migration, `svc_config_sanitize()` runs and enforces: NUL-terminated
  strings, booleans normalized to 0/1, auth forced on when provisioned, timing
  clamped to safe min/max, Modbus slave in 1..247, relay safe state forced
  de-energized, out-of-range rules disabled.
- A CRC-valid but semantically unsafe blob is sanitized, not trusted.

## 6. OTA

- A freshly booted OTA image is marked valid **only** after the health gate
  passes (control task alive, presence ran or degraded cleanly, relay safe state
  applied, network settled, watchdog stable, no fault latched). Never on a fixed
  timer.
- If the gate is not satisfied within the boot window, the image is left
  unconfirmed and the bootloader rolls back.

## 7. Known residual risks (track to closure)

- Setup password is stored/compared in plaintext NVS — migrate to a salted hash
  (PBKDF2/argon2) and a short-lived session token.
- Transport is HTTP, not HTTPS — auth/CSRF travel in cleartext; safe only on a
  trusted AP/LAN. Add `esp_https_server` before any routed exposure.
- No auth/provisioning rate-limiting — add attempt throttling/lockout.
- AP-only is enforced by policy, not transport (server still listens on all
  interfaces) — true interface binding is a `netmgr` TODO (RG-7).

## 8. Review checklist (Codex)

Auth cannot be disabled by config · provisioning gated by explicit mode · no
password in URL · JSON escaping bounded · NVS config sanitized · relay/IO routes
protected · no `portMAX_DELAY` in control-critical paths · OTA validated by
health, not delay.
