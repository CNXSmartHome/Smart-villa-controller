# 01_POWER — Schematic Sheet

## Purpose

Generate protected power rails for SVC-100.

## Inputs

- VIN_24V
- GND

## Outputs

- VIN_PROTECTED
- +5V
- +3V3
- SENSOR_V+

## Circuit Blocks

### Input Protection

- Fuse/PTC: F1
- Reverse polarity protection: ideal diode PMOS or series Schottky for prototype
- TVS diode: D_TVS_IN
- Bulk capacitor: C_IN_BULK

### 5V Buck

- Input: VIN_PROTECTED
- Output: +5V
- Target current: 1.5A minimum, 2A preferred

### 3.3V Regulator

- Input: +5V
- Output: +3V3
- Target current: 700mA+ for ESP32 Wi-Fi burst

### Sensor Power

- SENSOR_V+ from VIN_PROTECTED through fuse/PTC
- Connector output to RS485 sensor ports

## Test Points

- TP_VIN
- TP_5V
- TP_3V3
- TP_GND

## Notes

ESP32 3.3V stability is critical. Use adequate capacitance close to the module.
