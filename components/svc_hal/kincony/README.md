# hal_kincony — KinCony KC868-A8 / A16 / E16 target (SKELETON)

Implements `hal.h` for KinCony ESP32 boards. NOT YET IMPLEMENTED — placeholder.

Key board facts to encode (source from KinCony schematics/docs — REQUIRED input):
- Relays + digital inputs are behind **I2C PCF8574 expanders**, not native GPIO
  (`HAL_CAP_IO_EXPANDER`). hal_relay_write_mask / hal_din_read_mask do one I2C
  transaction across the expander(s).
- ESP32 (classic), not ESP32-S3: no native USB; UART bridge for console.
- RS485 on the board's dedicated UART + DE pin.
- A8 = 8 relays / 8 inputs; A16 = 16 / 16; E16 = ESP32 expandable.
- Confirm expander I2C addresses, relay drive polarity, and the power-on/expander
  -reset default output state (must be relay DE-ENERGIZED).
