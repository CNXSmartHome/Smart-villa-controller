# Smart Villa Controller — SVC-100 Firmware

Standalone industrial controller firmware for pool villas, hotels, and smart homes.
Built on the **ESP32-S3-WROOM-1-N16R8** with **ESP-IDF 5.x** and **FreeRTOS**.

The controller runs fully standalone (no Home Assistant required), exposes a local
Web UI, supports OTA firmware updates, and optionally integrates with MQTT and a
REST API.

## Hardware (V1)

| Block            | Detail                                                   |
|------------------|----------------------------------------------------------|
| MCU              | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB PSRAM)         |
| Power            | 12–24 VDC input                                          |
| RS485            | 2x isolated half-duplex (Modbus RTU master)             |
| Relay output     | 4x dry-contact                                           |
| Digital input    | 8x dry-contact (opto-isolated)                           |
| Connectivity     | Wi-Fi (always), Ethernet (optional / DNP'd)             |
| USB              | USB-C (CDC console + flashing)                           |
| RTC              | External I2C RTC + SNTP                                  |
| HMI              | Buzzer, status RGB LED, Config button, Reset button     |

## Key capabilities

- Read mmWave human-presence sensor over RS485 Modbus RTU.
- Read 8 dry-contact digital inputs (debounced, edge events).
- Drive 4 dry-contact relay outputs with safe default/failsafe states.
- Local rule engine (presence + inputs -> outputs) that works offline.
- Local Web UI for live status, manual override, and configuration.
- OTA firmware update with rollback.
- Optional MQTT publish/subscribe and REST API.

## Repository layout

```
svc-100-firmware/
├── CMakeLists.txt            # top-level project
├── sdkconfig.defaults        # baseline ESP-IDF configuration
├── partitions.csv            # dual-OTA + NVS + storage partition table
├── main/                     # application entry + wiring
└── components/               # one folder per firmware module (lib)
    ├── svc_common/           # result/error types, logging macros, utils
    ├── board/                # board pin map + low-level HAL init
    ├── eventbus/             # decoupled inter-module event bus
    ├── indicator/            # status LED + buzzer state machine
    ├── button/               # config/reset button handler
    ├── relay/                # relay output driver
    ├── dinput/               # debounced digital input driver
    ├── rs485/                # RS485 UART transport (half-duplex/DE control)
    ├── modbus_master/        # Modbus RTU master on top of rs485
    ├── presence/             # mmWave presence sensor service
    ├── storage/              # NVS-backed configuration store
    ├── netmgr/               # Wi-Fi / Ethernet connection manager
    ├── webui/                # HTTP server + REST + static UI
    └── control/              # rule engine wiring inputs -> outputs
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/MODULES.md`](docs/MODULES.md),
and [`docs/BUILD.md`](docs/BUILD.md).

## Quick build

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## License

Proprietary — © 2026. All rights reserved.
