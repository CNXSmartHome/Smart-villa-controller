# Review Request — SVC-026 CI build matrix + per-target sdkconfig

- **Date:** 2026-07-03
- **Author:** Claude (implementer)
- **Reviewer:** Codex
- **Scope:** CI multi-target build matrix (esp32s3 now, esp32/KinCony prepared) and
  per-target `sdkconfig.defaults` split. No firmware `.c`/`.h` changes.
- **Patch:** `svc026_ci_matrix.patch` (diff of `build.yml`, `sdkconfig.defaults`,
  `AI_TASK_QUEUE.md` + full text of the two new per-target files).

## Files

| File | Change |
|------|--------|
| `.github/workflows/build.yml` | Rews into `host-tests` gate → `build` **matrix** (`idf.py set-target <t>` + build + `idf.py size`). esp32s3 active; esp32 leg commented. |
| `sdkconfig.defaults` | Reduced to **target-neutral** options + default `CONFIG_IDF_TARGET="esp32s3"`. |
| `sdkconfig.defaults.esp32s3` (new) | SVC-100 Rev A: 16 MB QIO flash, octal PSRAM, USB-Serial-JTAG console, `CONFIG_SVC_TARGET_SVC100_REVA=y`. |
| `sdkconfig.defaults.esp32` (new) | KinCony KC868-A8 (future): 4 MB DIO flash, no PSRAM, UART console, `CONFIG_SVC_TARGET_KINCONY_A8=y`. Commented-out partition override + bring-up TODO. |

## Design notes

- ESP-IDF auto-merges `sdkconfig.defaults.<idf_target>` after `sdkconfig.defaults`,
  so `idf.py set-target esp32s3` (default) or `esp32` selects the right overlay
  with no CMake changes.
- Default target stays esp32s3 so a plain local `idf.py build` is unchanged.
- esp32/KinCony leg is intentionally NOT in the active matrix: app_main still boots
  the SVC-100 board path (ESP32-S3-only pins + USB-Serial-JTAG), so an esp32 link
  would fail until the HAL is wired (SVC-024/025). The overlay + a commented matrix
  entry are in place so enabling it later is a one-line change; `experimental` /
  `continue-on-error` keeps CI green during bring-up.
- The shared `partitions.csv` is a 16 MB dual-3 MB-OTA layout that does NOT fit the
  KC868-A8's 4 MB flash — a `partitions_kincony.csv` is a prerequisite for the
  esp32 leg (documented in `sdkconfig.defaults.esp32`).

## Verification done here
- `python3 -c 'yaml.safe_load(...)'` → `build.yml` valid YAML.
- **Config-equivalence check:** union of `sdkconfig.defaults` + `sdkconfig.defaults.esp32s3`
  vs the previous single `sdkconfig.defaults` (git HEAD) differs by exactly
  `+CONFIG_SVC_TARGET_SVC100_REVA=y` (which was already the Kconfig default) — so
  the effective esp32s3 build is unchanged.
- `sh tests/host/run.sh` → ALL PASS.
- `sdkconfig` / `sdkconfig.old` confirmed git-ignored (CI checkout is clean).
- Full `idf.py build` for the matrix must run in CI / on the Mac (idf.py absent in
  the review sandbox); the prior esp32s3 `idf.py build` already PASSED.

## Round 2 — Codex fix (workflow line 25)

Codex withheld approval on `build.yml` line 25: the host-tests step invoked the
**bash** script with `sh` (`run: sh tests/host/run.sh`). `tests/host/run.sh` uses
`set -o pipefail` and bash arrays; on `ubuntu-latest` `/bin/sh` is **dash**, so the
step would fail immediately (it only passed locally because macOS `sh` is bash).

**FIX:** `run: bash tests/host/run.sh`.

Proven in the review sandbox (also dash-as-/bin/sh): `dash tests/host/run.sh` →
exit 2, `set: Illegal option -o pipefail` (line 12); `bash tests/host/run.sh` →
`ALL PASS`. With this change the host-tests gate will run correctly on GitHub, so
SVC-026 CI matrix is ready for its first run.

## Please confirm
- Matrix shape and the host-tests → build ordering.
- The target-neutral vs per-target split is correct (nothing S3-only left in the
  common file; nothing missing for a clean esp32s3 build).
- The esp32 overlay values (flash 4 MB DIO/40 MHz, no PSRAM, UART console) and the
  recorded prerequisites before enabling that leg.
