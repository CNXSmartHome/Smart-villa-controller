# 02_ESP32_S3_CORE — Schematic Sheet

## MCU

ESP32-S3-WROOM-1-N16R8

## Required Connections

- +3V3
- GND
- EN reset circuit
- BOOT button
- USB D+/D-
- UART/debug optional
- GPIO mapped to board functions

## Boot / Reset

- EN pull-up
- Reset button to GND
- BOOT button on required boot GPIO per ESP32-S3 reference design

## Critical Review

- Confirm GPIO15/16/17/47/48 availability and boot behavior
- Confirm USB native wiring
- Confirm antenna keepout
- Confirm no relay control pin energizes on boot

## Firmware Rule

All GPIO definitions must be centralized in `components/board/include/board.h`.
