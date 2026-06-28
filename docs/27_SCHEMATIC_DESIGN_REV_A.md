# SVC-100 Pro — Schematic Design Rev A

## Design Intent

This document defines the schematic-level design for SVC-100 Pro Rev A.

Rev A is a functional engineering prototype, but must follow production-oriented safety and reliability rules.

## Sheet Structure

1. `01_POWER`
2. `02_ESP32_S3_CORE`
3. `03_RS485`
4. `04_RELAY_OUTPUTS`
5. `05_DIGITAL_INPUTS`
6. `06_USB_DEBUG`
7. `07_RTC_LED_BUZZER`
8. `08_ETHERNET_OPTION`
9. `09_CONNECTORS_TESTPOINTS`
10. `10_EXPANSION_RESERVED`

## Rev A Scope

Baseline Rev A:

- ESP32-S3-WROOM-1-N16R8
- 24VDC input
- 5V buck
- 3.3V regulator
- RS485 x2 baseline, footprint reserve for x3
- Relay x4 baseline, layout reserve for x8 if enclosure allows
- DI x8 baseline, header reserve for x12
- USB-C programming/debug
- RTC
- Status LEDs
- Buzzer
- Config/Reset buttons
- DIN rail form factor

## Schematic Release Gates

Before PCB layout:

- ERC clean
- All connector pins named
- All power rails named
- All boot strap pins reviewed
- Relay safe-off state verified electrically
- RS485 termination/bias options included
- Test points added
- Power budget reviewed
- BOM candidates listed
