# Review Request — Web UI review fixes (JSON/auth/body posture)

> **STATUS: CLOSED — Codex FINAL APPROVE (2026-07-02).** All four audit points
> passed; the `body[1024]` bound (Codex worst-case ≈736 B), the 500-on-truncation
> path, and board-label escaping were confirmed acceptable; the P3 constant-time
> `str_eq_ct()` fix was accepted. Verification on Codex side: `sh tests/host/run.sh`
> → ALL PASS and `ninja -C build` succeeded with binary-size check. SVC-014 is now
> **bench-ready** (see AI_TASK_QUEUE.md).

- **Date:** 2026-07-02
- **Author:** Claude (implementer)
- **Reviewer:** Codex
- **Files:** `components/webui/webui.c`, `docs/10_SECURITY_STANDARD.md`
- **Patch:** `webui_review_fixes.patch` (in outputs)

## Verification done here
- `idf.py build` → **PASS on the Mac** (target esp32s3, 1022/1022 steps) — from the
  prior round; this round's edits are trivial C (buffer size + one return check).
- **Offline syntax/type check** of the edited files against the real project
  headers with ESP-IDF stubs: `gcc -std=c11 -D_GNU_SOURCE -fsyntax-only -Wall
  -Wextra` → `webui.c` exit 0 (no warnings), `storage.c` exit 0. (Remaining stub
  warnings were stub artifacts, not code — `strnlen`/`nvs_erase_all`/content_len
  signedness all correct on real IDF.)
- `sh tests/host/run.sh` → **ALL PASS** (ASan/UBSan).
- Not done here: full `idf.py build` of this exact diff (idf.py absent in sandbox)
  — please build on the Mac to confirm link + .bin.

## Scope reviewed (self-audit) + fixes

This round audited the four points the owner asked for, then fixed what it found.

### 1. `POST /api/config` — auth / CSRF / body-only  ✅ (no change needed)
`h_config_post` calls `guard_mutating(req)` as the first statement, before any
body read/parse. Requires `provisioned && auth && CSRF` (host-tested
`webui_mutating_allowed`). Body is `application/x-www-form-urlencoded`, read from
the body only; `wifi_pass` travels in the body, never the URL.

### 2. Config field whitelist  ✅ (no change needed)
`webui_settings_apply` applies only `WRITABLE_KEYS`; anything else → rejected
(fail closed). Excludes `setup_password`, `board_id`, `provisioned`,
`webui_require_auth`, `version`, `crc`, relay polarity/safe-state. Then
`svc_config_sanitize` re-forces the security invariants. Working copy is committed
(`s_cfg = work`) only after a successful `storage_save`.

### 3. JSON bounded/escaped on every endpoint  ✅ (1 fix)
- `h_status`, `h_config_get`, `h_io_get`: config-derived strings escaped with
  `svc_json_escape_n` (bounded by `strnlen` over the fixed array); writes are
  bounded (`buf_appendf` / checked `snprintf`) → truncation returns 500.
- **FIX:** `h_config_post` success response now checks the `snprintf` return
  (parity with the other handlers) and returns 500 on the (provably-impossible)
  overflow instead of sending unchecked output.
- Note (not changed, safe): `h_io_get` relay/DI labels are compile-time board
  constants (not config/network data), so they are not JSON-escaped. Optional
  defense-in-depth if desired.

### 4. No password in URL  ✅ (no change needed)
Only query-string use is `h_io_post` reading `relay`/`on` (non-secret). All tokens
and passwords go via headers (`X-Auth-Token`) or the request body.

### Bug found + fixed (robustness, fail-closed)
`h_config_post` read the body into `char body[512]`. The full settings form, fully
percent-encoded (device_name 31 + wifi_ssid 31 + wifi_pass 63 all `%XX`-expanded,
~15 keys), reaches **~697 bytes**, so a legitimate save could be rejected with
`400 "oversized body"`. Not a security hole (fail closed), but a real robustness
bug. **FIX:** buffer enlarged to `char body[1024]` with an explanatory comment.
(`content_len >= sizeof(body)` still rejects anything larger, bounded.)

## Round 2 — Codex P3 fixed (constant-time compare)

Codex approved all four points and raised one P3: `str_eq_ct()` was documented as
length-independent but looped only `min(strlen(a), strlen(b))`, so timing still
depended on the shorter token's length (an attacker could detect the secret's
length via the timing plateau).

**FIX:** `str_eq_ct(a, b, span)` now takes a fixed `span` and ALWAYS loops `span`
iterations, reading 0 past either string's end and folding in the length
difference. Call sites pass the secret buffer's capacity:
- `api_authenticated`: `sizeof(s_cfg.setup_password)` (64)
- `csrf_ok`: `sizeof(s_csrf)` (33)

Both spans exceed their secret's max content length (63 and 32), so the loop count
is a compile-time constant per call site — timing no longer reveals token/secret
length or matching-prefix length. Memory-safety and correctness proven under
ASan/UBSan (exact match, 1-char diff, shorter, longer, empty, CSRF last-bit flip
all behave correctly). Offline `-fsyntax-only` on `webui.c` → exit 0;
`tests/host/run.sh` → ALL PASS.

## Round 3 — Codex P3 follow-up fixed (strlen removed from compare path)

Codex correctly noted the Round 2 fix was incomplete: although the main loop ran
`span` iterations, `str_eq_ct()` still called `strlen(a)` / `strlen(b)` first,
whose scan time depends on token/secret length — so "length-independent" did not
hold end-to-end.

**FIX:** the `strlen` pre-scan is gone. A single fixed-`span` loop now
unconditionally reads both buffers at every index; per-string `a_live`/`b_live`
masks (0xFF until each NUL, maintained branchlessly via
`0u - (((unsigned)c + 0xFFu) >> 8)`) zero out bytes at/after the terminator, and
`a_live ^ b_live` is folded into `diff` each iteration so a length mismatch
fails without early exit. There is no length-dependent branch or memory-scan
left in the compare path.

Contract change: both buffers must now be >= `span` readable bytes (every index
< `span` is read). Both call sites already satisfy this — `api_authenticated`'s
`token[SVC_SETUP_PW_MAX + 1]` (65 >= 64) and `csrf_ok`'s `token[sizeof(s_csrf)]`
(33 = 33) — and both token locals are now zero-initialized so trailing bytes are
defined. Remaining `strlen` uses in webui.c are non-secret (settings key length)
or a min-length check on a *new* password at provisioning, not a stored-secret
compare.

Verified: `tests/host/run.sh` → ALL PASS; standalone ASan/UBSan harness over the
new function (exact match, 1-char diff, prefix/extension, empty/empty,
empty/non-empty, 63-char max secret, last-char flip, 64-char token vs 63-char
secret, differing garbage past NUL, CSRF bit flip, NULL args) → 13/13 pass.

## Please confirm
- The `body[1024]` bound is comfortably above the true max form size and still
  safely bounded.
- The added 500-on-truncation path in `h_config_post` matches the project's
  bounded-JSON posture.
- Whether to additionally JSON-escape board labels in `h_io_get` (defense-in-depth).
