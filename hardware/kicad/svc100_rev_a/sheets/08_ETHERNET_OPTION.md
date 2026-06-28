# 08_ETHERNET_OPTION — Schematic Sheet

## Purpose

Reserve Ethernet design path.

## Options

### Option A: LAN8720

- RMII PHY
- Native ESP-IDF support
- Requires careful GPIO allocation and clocking

### Option B: W5500

- SPI Ethernet
- Simpler routing than RMII
- Higher BOM cost
- Uses SPI pins

## Rev A Decision

Keep Ethernet optional until GPIO and firmware decision is finalized.

## Release Gate

Do not finalize PCB until Ethernet option is decided or clearly marked DNP.
