# components/svc_hal — Smart Villa OS HAL (interface)

The ESP-IDF component is named `svc_hal` so it does not shadow ESP-IDF's
internal `hal/...` headers.

`include/hal.h` is the **target-independent contract** the services call. One
target component is compiled per build (Kconfig `CONFIG_SVC_TARGET_*`):

```
components/svc_hal/
  include/hal.h   <- the contract (always)
components/hal_svc100/
  hal_svc100.c    <- SVC-100 Rev A: native ESP32-S3 GPIO / RMT / LEDC   [Phase 2]
components/hal_kincony/
  hal_kincony.c   <- KinCony KC868-A8/A16/E16: I2C PCF8574 expander IO  [Phase 1]
```

Status: **interface proposal only** — implementations are skeletons, nothing is
wired into relay/dinput/rs485/button/indicator yet, behavior is unchanged. See
`docs/30_HAL_ARCHITECTURE.md` for the audit, API name mapping, and migration plan.
