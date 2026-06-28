# Firmware Architecture

## Framework

- ESP-IDF 5.x
- C/C++
- FreeRTOS

## Components

- board
- storage
- config
- relay
- dinput
- rs485
- modbus_master
- presence
- control
- eventbus
- webui
- ota
- security
- indicator
- fault_manager

## Runtime Tasks

- Control task
- Presence polling task
- Web server task
- OTA task
- Indicator task
- Watchdog/health task
