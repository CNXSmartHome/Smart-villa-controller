# 10 — Bench Test Procedure (SVC-014 Web UI settings + config v5)

Gate procedure to validate the installer Web UI settings path (**SVC-014**) and
the config **schema v5** migration on real hardware, before SVC-014 is marked Done
and merged for production. Complements `09_Bench_Test_Procedure.md` (SVC-010,
logic engine) — run this one for the Web UI / config-write / migration surface.

Drafted by Claude for Codex / CTO execution and sign-off. Subordinate to
`docs/00_PROJECT_BIBLE.md` and `docs/10_SECURITY_STANDARD.md`.

> SAFETY: bench only. Use **non-hazardous loads** (status LED, buzzer, or a
> low-voltage indicator lamp per relay). Do NOT connect mains or any hazardous
> load. This procedure exercises config writes and reboots, not relay control,
> but keep the relay-driver external pull-down (RG-6) populated regardless.

## 0. Preconditions

- [ ] Host unit tests pass: `bash tests/host/run.sh` → `ALL PASS`, exit 0.
- [ ] `idf.py build` clean for `esp32s3` from the commit under test.
- [ ] USB-C console attached; serial log captured for every step.
- [ ] A client on the **same LAN/AP** as the device with `curl` (or the built-in
      settings page). Record the device IP.
- [ ] The setup password is known (device already provisioned) OR plan to
      provision first via the physical config-button-at-boot path.

Shorthand used below (fill in IP + password):

```bash
IP=192.168.x.x
PW='<setup-password>'
A="-H X-Auth-Token:$PW"
# CSRF is per-boot; fetch it after auth:
CSRF=$(curl -s $A http://$IP/api/csrf | sed 's/.*"csrf":"\([^"]*\)".*/\1/')
C="-H X-SVC-CSRF:$CSRF"
```

## 1. Auth + CSRF gate on POST /api/config (SVC-014)

- [ ] **No auth →** `curl -s -o /dev/null -w '%{http_code}\n' -X POST http://$IP/api/config -d 'device_name=x'` returns **401**.
- [ ] **Auth, no CSRF →** same with `$A` only returns **403** (`missing/invalid CSRF`).
- [ ] **Auth + CSRF →** with `$A $C -d 'device_name=BenchVilla'` returns **200**
      `{"ok":true,"applied":1,"rejected":0}`.
- [ ] Wrong token (`-H 'X-Auth-Token:nope'`) → **401**; timing feels constant
      regardless of token length (constant-time compare; qualitative check only).

## 2. Whitelist is fail-closed (no privilege escalation)

Send a body that mixes a legit field with protected/secret ones:

```bash
curl -s $A $C -X POST http://$IP/api/config \
  --data 'device_name=WL&provisioned=false&webui_require_auth=0&setup_password=hack&board_id=kincony_kc868_a8&relay_safe_on=255'
```

- [ ] Response is **200** with `applied:1, rejected:5` (only `device_name` applied).
- [ ] `GET /api/config` afterwards shows the device **still provisioned**,
      `device_name` = `WL`, and board identity unchanged.
- [ ] Re-auth with the ORIGINAL password still succeeds (setup_password NOT changed).
- [ ] Serial log shows `settings updated (applied=1 rejected=5)`.

## 3. GET /api/config — auth-gated, no secrets

- [ ] **No auth →** `GET /api/config` returns **401**.
- [ ] **With `$A` →** 200 JSON. Confirm it does **NOT** contain `wifi_pass`,
      `setup_password`, or `mqtt_pass` (grep the response — must be absent).
- [ ] Field names match the form (`device_name`, `wifi_ssid`, `presence_1_*`, …)
      so the settings page populates.

## 4. Password only in body, never URL

- [ ] Set a Wi-Fi password via the settings page (or `--data 'wifi_pass=...'`).
      Capture the request (browser devtools / `tcpdump`): the value appears **only**
      in the POST body, never in the URL/query string.
- [ ] Device serial log and any access log show **no** password in a URL.

## 5. Bounded body + bounded JSON

- [ ] Save the **full** settings form with a max-length Wi-Fi password (63 chars)
      and 31-char device_name/SSID → returns **200** (body[1024] holds the fully
      percent-encoded form; the earlier 512-byte limit would have 400'd this).
- [ ] Oversized body (`>1024` bytes) → **400** `missing/oversized body` (fail closed).
- [ ] Responses are valid JSON (no truncation) for `GET /api/config`, `GET /api/io`,
      `GET /api/status`.

## 6. Persistence across reboot

- [ ] After a successful save, power-cycle (and separately, EN reset). `GET /api/config`
      returns the saved `device_name` / settings — values survived NVS.
- [ ] `svc_config_sanitize` clamps are visible: e.g. write `presence_1_modbus_addr=0`
      (invalid) → after save the sensor is disabled/clamped, not stored raw.

## 7. Config v5 migration on real flash (the unverified item)

Pre-load NVS with older-schema blobs (flash a saved v2/v3/v4 dump, or a build
that wrote them), then boot the v5 image:

- [ ] **v1 → v5**: log `migrated config v1 -> v5 (re-provisioning required)`;
      device comes up **un-provisioned** (v1 had no security fields). `was_default`
      path: device requires provisioning.
- [ ] **v2/v3/v4 → v5**: log `migrated config vN -> v5`; provisioning + setup
      password **preserved** (still provisioned, original login works). **Confirm
      the device is NOT forced to factory defaults** — this validates the Codex fix
      that migrated configs are no longer flagged `was_default=true`.
- [ ] **Wrong-board config** (board_id mismatch): log shows board-guard rejecting
      it → factory default + stamps the current `CONFIG_SVC_BOARD_ID`; comes up
      un-provisioned and safe.
- [ ] **Corrupt CRC**: `config CRC bad; using defaults` → safe, un-provisioned.
- [ ] Multi-sensor block present after migration: `presence_sensor[0]` mapped from
      the legacy single-sensor fields; sensor[1] disabled by default.

## 8. Provisioning lockout (no LAN provisioning)

- [ ] On a **provisioned** device, `POST /api/provision` → **409/403** (never
      re-provisions silently over LAN).
- [ ] Provisioning succeeds ONLY via the physical config-button-at-boot (or AP mode
      if implemented). No ambient-LAN window.

## 9. Decision gate (SVC-014 → Done)

All boxes in §1–§8 checked, host tests green, `idf.py build` clean. Specifically:

- [ ] Mutating config requires auth+CSRF; whitelist rejects all protected/secret fields.
- [ ] Secrets never returned; passwords never in URL.
- [ ] v2/v3/v4 migration preserves provisioning (was_default fix confirmed on NVS).
- [ ] Settings persist across reboot; sanitize clamps unsafe values.

## Sign-off

| Role | Name | Result (PASS/FAIL) | Date | Notes |
|------|------|--------------------|------|-------|
| Executor (bench) | | | | |
| Codex (review) | | | | |
| CTO (sign-off) | | | | |
