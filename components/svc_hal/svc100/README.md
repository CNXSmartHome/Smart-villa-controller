# hal_svc100 — SVC-100 Pro Rev A target (SKELETON)

Implements `hal.h` for SVC-100 Rev A (ESP32-S3, native GPIO). This is a thin
re-wrapping of the EXISTING `components/board` + the GPIO bodies currently inside
relay.c / dinput.c / rs485.c / button.c / indicator.c — no logic change, just
moved behind hal_*. NOT YET IMPLEMENTED — placeholder.

Facts: 4 relays (GPIO4-7), 8 inputs, 2x RS485, WS2812 RGB LED (HAL_CAP_RGB_LED),
LEDC buzzer, native USB-Serial-JTAG. Pin map = hardware/pin_database + board.h.
