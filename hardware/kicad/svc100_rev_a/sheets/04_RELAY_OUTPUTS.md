# 04_RELAY_OUTPUTS — Schematic Sheet

## Purpose

Provide dry contact relay outputs for control circuits.

## Rev A Target

- Relay x4 populated
- Layout reserve for relay x8 if mechanical space permits

## Per Relay

- MCU GPIO -> transistor/MOSFET driver
- Flyback diode or integrated driver protection
- Relay coil supply
- Contact terminal: COM / NO / NC
- Optional relay status LED

## Electrical Safety

- Relay default OFF/safe
- GPIO must not float active during boot
- Separate relay contact routing from logic
- Use adequate clearance if switching higher voltage

## Recommended Usage

Use relay to control:
- AC dry contact/keycard input
- Contactor coil
- Lighting control input
- Fan/exhaust control input

Do not directly switch high-current loads in Rev A unless final relay and PCB clearance support it.
