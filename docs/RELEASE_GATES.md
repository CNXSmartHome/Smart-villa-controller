
## RG-K1 — KinCony PCF8574 relay safe-state (MVP) — OPEN

`hal_kincony` assumes the KC868 relay modules are ACTIVE-LOW (PCF8574 output 0
energizes; idle/safe byte = 0xFF) and assumes the expander I2C addresses
(relay 0x24[/0x25], input 0x21/0x22) and RS485 pins. **Verify on a physical
KinCony board before trusting any relay:** confirm the PCF8574 power-on / reset
default output state maps to relays DE-ENERGIZED, confirm polarity and addresses.
An I2C fault must (and does in code) return a fail-safe error so the control loop
applies the safe state. Do not connect 220 V loads until RG-K1 is closed.
