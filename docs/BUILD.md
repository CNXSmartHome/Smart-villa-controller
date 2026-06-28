# SVC-100 Build System Plan

## Toolchain

- **ESP-IDF 5.x** (5.2 LTS or later). Target: `esp32s3`.
- Component-based CMake build (`idf.py`). No Arduino, no PlatformIO.
- C standard: C11; C++ standard: C++17 (only where a component opts in).

## Top-level structure

```
CMakeLists.txt          # project() + include $IDF_PATH/tools/cmake/project.cmake
sdkconfig.defaults      # tracked baseline config (sdkconfig itself is .gitignored)
partitions.csv          # custom partition table (dual OTA + nvs + storage)
main/CMakeLists.txt     # idf_component_register for the app entry
components/<m>/CMakeLists.txt   # per-module idf_component_register
```

Each component declares `SRCS`, `INCLUDE_DIRS include`, and `REQUIRES` (public deps) /
`PRIV_REQUIRES` (private deps). Public-vs-private `REQUIRES` keeps include graphs tight
and link times low.

## Configuration baseline (`sdkconfig.defaults`)

- Flash: 16 MB, QIO, 80 MHz; PSRAM: octal, enabled.
- Partition table: custom CSV.
- FreeRTOS: TWDT enabled, panic on TWDT timeout, unicore disabled (dual-core).
- Log default level: INFO (per-module overridable via menuconfig).
- HTTP server, mbedTLS, and OTA enabled for the optional service layer.

Developers override locally with `sdkconfig.defaults.esp32s3` or `menuconfig`; the
generated `sdkconfig` is **not** committed.

## Partition layout (`partitions.csv`)

Dual application slots (`ota_0` / `ota_1`) with an `otadata` partition enable
A/B OTA with rollback. A dedicated `nvs` namespace holds configuration; a `storage`
(FAT/SPIFFS/LittleFS) partition holds the Web UI static assets and logs.

## Build / flash / debug

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3            # once
idf.py menuconfig                    # optional local tweaks
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
idf.py size-components               # per-component footprint
```

OTA image for field update is `build/svc-100.bin`; serve it from `/api/ota` or any
HTTPS endpoint for `esp_https_ota`.

## Quality gates (CI-ready)

| Gate              | Tool                                             |
|-------------------|--------------------------------------------------|
| Format            | `clang-format` (LLVM-derived `.clang-format`)    |
| Static analysis   | `idf.py clang-check` / `cppcheck`                |
| Host unit tests   | logic modules via `components/<m>/test` + CMock  |
| On-target tests   | `unity` test app under `test_app/`               |
| Build matrix      | `idf.py build` on pinned IDF version in CI        |

## Testability seams

- `svc_common`, `modbus_master` CRC/PDU, and `dinput` debounce are pure C with no
  IDF driver calls in the core logic — compile & test on host with Unity/CMock.
- Hardware modules expose narrow function-pointer or weak-symbol seams so the bus
  (`rs485_txn`) can be mocked under test.

## Versioning

`svc_common/svc_version.h` carries `SVC_FW_VERSION` (semver). The build also embeds the
IDF `app_desc` (git hash, build date) used by the OTA self-report and Web UI.
