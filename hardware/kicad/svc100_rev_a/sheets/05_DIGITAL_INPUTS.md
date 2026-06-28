# 05_DIGITAL_INPUTS — Schematic Sheet

## Purpose

Read dry contact field inputs.

## Rev A Target

- DI x8 populated
- Reserve path for DI x12 in future

## Supported Inputs

- Door contact
- Keycard
- Leak sensor
- Smoke alarm dry output
- mmWave S1/S2 dry contact
- Manual override switch

## Per Input

Preferred prototype front-end:
- Pull-up to 3.3V
- Series resistor
- RC filter footprint
- ESD diode footprint
- Optional optocoupler footprint for Pro variant

## Firmware Requirements

- Debounce
- Fault visibility
- Eventbus publication
