# Review Request — SVC-014 Web UI Settings Page (+ review fixes)

- **Date:** 2026-07-02
- **Author:** Claude (implementer)
- **Reviewer:** Codex
- **Status:** Ready for review → bench/merge gated on this review
- **Verification:** `sh tests/host/run.sh` → `ALL PASS` (ASan/UBSan on Linux; sanitizers auto-off on macOS)
- **Not verified here:** ESP-IDF `idf.py build` (idf.py absent in sandbox — must run on the Mac); on-target bench.

## 1. Scope

Primary task **SVC-014** plus fixes for two prior Codex review rounds. Constraints
honored: ESP-IDF only (no Arduino); all mutating APIs require auth after
provisioning; no provisioning from normal LAN; no password in URL/query; changes
scoped; docs/tests updated.

## 2. SVC-014 — what to review

**New, pure, host-tested module (security boundary):**
- `components/webui/webui_settings.h/.c` — `webui_settings_is_writable(key)` +
  `webui_settings_apply(cfg, key, value)`. Write **whitelist** (`WRITABLE_KEYS`).
  Non-listed keys are rejected fail-closed. Whitelist deliberately **EXCLUDES**
  `board_id`, `version`, `crc`, `provisioned`, `setup_password`,
  `webui_require_auth`, and relay polarity/safe-state — a settings write can
  never weaken auth or store an unsafe relay state.

**Handler + route (`components/webui/webui.c`):**
- `POST /api/config`: runs `guard_mutating` (auth + CSRF) **before** parsing;
  body is `application/x-www-form-urlencoded` (added `url_decode`/`hexval`);
  applies to a **working copy** → `svc_config_sanitize` → `storage_save` → atomic
  `s_cfg` swap → `control_reload_config`. Response `{"applied":N,"rejected":M}`.
- `GET /api/config`: now emits installer field names (no secrets:
  `wifi_pass`/`setup_password` never returned) so the page can populate.
- Installer settings page: setup password held in memory only, sent as
  `X-Auth-Token` header, **never** in a URL; loads `/api/csrf` then `/api/config`.

**Tests/build/docs:** `tests/host/test_main.c` `test_webui_settings()` (protected
fields rejected without mutating cfg; sanitize clamps bad values);
`webui_settings.c` added to component `CMakeLists.txt` + `tests/host/run.sh`;
`docs/10_SECURITY_STANDARD.md` documents the route.

### Review focus points
- Confirm the whitelist truly cannot reach any auth/security field.
- Confirm body parsing is bounded (no over-read on un-terminated body).
- Confirm password never appears in URL/logs.
- Confirm working-copy → sanitize → save → swap ordering is atomic-ish (live
  `s_cfg` only replaced on successful save).

## 3. Review fixes since last round (please re-confirm)

**Round A (all previously verified by Codex):**
- HAL `Kconfig` default reverted to `SVC_TARGET_SVC100_REVA` (app_main still boots
  SVC-100 path; KinCony default would stamp wrong `board_id`).
- `README.md` restored from HEAD (BOM note had overwritten it).
- `hal.h` reduced to a thin `#include "hal_board.h"` (single HAL contract;
  removed duplicate 16-relay `hal_relay_get_count()` API). Doc table updated.
- `storage_load()` no longer forces `was_default=true` on successful migration
  (v2/v3/v4 preserve provisioning; migrated ≠ factory default). **NVS/IDF-bound —
  needs bench confirmation; host tests only mirror the version/size decision.**

**Round B (this round):**
- Restored the full KiCad Rev A project shell: `svc100_rev_a.kicad_pro`,
  top-level `.kicad_sch`, `.kicad_pcb`, sheets `02`–`10`, `README.md`,
  `docs/KICAD_PROJECT_NOTES.md`. The new detailed `sheets/01_POWER.kicad_sch`
  (+`01_POWER_DESIGN.md`, `gen_01_power.py`) is kept inside the project.
  Top-level references all 10 sheets — project opens as a full Rev A project.
- Restored `hardware/schematic/*` (sheets 01–10, `net_names_rev_a.csv`,
  `schematic_checklist_rev_a.md`) — DR-0002 "source of truth" citation resolves.
- `git status`: no remaining deletions.

## 4. Files

Patch of tracked-file changes: `svc014_review.patch` (in outputs).
New files (read directly in-tree): `components/webui/webui_settings.{h,c}` and the
new `components/{hal,hal_kincony,hal_svc100,logic,presence_fusion,control}` +
`docs/*` listed in `git status`.

## 5. Open items (not blockers for this review, tracked in AI_TASK_QUEUE.md)
- ESP-IDF build + on-target bench (workstation only).
- Bench-confirm the `was_default` migration behavior.
- SVC-024/025: wire app_main/services through `hal_install()`/`hal_board_init()`
  before any KinCony target can become the build default.
