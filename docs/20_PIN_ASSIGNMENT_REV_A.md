# 20 — Pin Assignment (Rev A)

Human-readable mirror of the firmware GPIO authority,
`components/board/include/board.h`. The machine-readable form is
`hardware/pin_database/svc100_pin_database_rev_a.csv`.

**Authority:** `board.h` is the single source of truth for firmware. Hardware
(KiCad Rev A) must reconcile against this table; discrepancies are resolved by
Owner + CTO, and `board.h` is updated in lock-step (a board revision touches only
the `board` component). This document does **not** change any GPIO.

## Assignment table

| Function | Macro | GPIO | Notes / constraints |
|---|---|---:|---|
| I2C SDA (RTC) | `BOARD_GPIO_I2C_SDA` | 1 | general IO |
| I2C SCL (RTC) | `BOARD_GPIO_I2C_SCL` | 2 | general IO |
| Relay 0 | `BOARD_GPIO_RELAY_0` | 4 | needs external pull-down — RG-6 |
| Relay 1 | `BOARD_GPIO_RELAY_1` | 5 | RG-6 |
| Relay 2 | `BOARD_GPIO_RELAY_2` | 6 | RG-6 |
| Relay 3 | `BOARD_GPIO_RELAY_3` | 7 | RG-6 |
| Digital in 4 | `BOARD_GPIO_DIN_4` | 8 | dry contact, pull-up |
| Digital in 5 | `BOARD_GPIO_DIN_5` | 9 | dry contact, pull-up |
| Digital in 6 | `BOARD_GPIO_DIN_6` | 10 | dry contact, pull-up |
| Digital in 7 | `BOARD_GPIO_DIN_7` | 11 | dry contact, pull-up |
| RS485-A TX (UART1) | `BOARD_GPIO_RS485A_TX` | 12 | free GPIO on S3 |
| RS485-A RX (UART1) | `BOARD_GPIO_RS485A_RX` | 13 | free GPIO on S3 |
| RS485-A DE/RE | `BOARD_GPIO_RS485A_DE` | 14 | driven by UART RTS (HW RS485 half-duplex) |
| Digital in 0 | `BOARD_GPIO_DIN_0` | 15 | boot/UART-adjacent — RG-1 |
| Digital in 1 | `BOARD_GPIO_DIN_1` | 16 | RG-1 |
| Digital in 2 | `BOARD_GPIO_DIN_2` | 17 | RG-1 |
| Digital in 3 | `BOARD_GPIO_DIN_3` | 18 | general IO |
| RS485-B TX (UART2) | `BOARD_GPIO_RS485B_TX` | 21 | free GPIO on S3 |
| RS485-B RX (UART2) | `BOARD_GPIO_RS485B_RX` | 47 | confirm — RG-5 |
| RS485-B DE/RE | `BOARD_GPIO_RS485B_DE` | 48 | confirm; WS2812 strap on some dev boards — RG-5 |
| Status LED (WS2812 RMT) | `BOARD_GPIO_LED_STRIP` | 38 | general IO |
| Buzzer (LEDC PWM) | `BOARD_GPIO_BUZZER` | 39 | pin-JTAG MTCK; USB-JTAG used instead |
| Config button | `BOARD_GPIO_BTN_CONFIG` | 40 | active-low pull-up; pin-JTAG MTDO |
| Reset button | `BOARD_GPIO_BTN_RESET` | 41 | active-low pull-up; pin-JTAG MTDI |

UART map: RS485-A = `UART_NUM_1`, RS485-B = `UART_NUM_2`, I2C = `I2C_NUM_0`.

## Reserved / must-not-use (ESP32-S3-WROOM-1-N16R8)

- **GPIO33–37** — in-package **octal PSRAM** (N16R8). Not available. (board.h uses none.)
- **GPIO26–32** — SPI0/1 flash. Not available. (board.h uses none.)
- **GPIO19 / GPIO20** — USB D-/D+ (USB-Serial-JTAG console). Kept free. (board.h uses none.)
- **GPIO0 / 3 / 45 / 46** — strapping pins. Not used for board functions.

## Open hardware confirmations (Release Gates)

- **RG-1** — GPIO15/16/17 reset level / boot behavior with the opto front-ends.
- **RG-5** — GPIO47/48 availability; produce the authoritative pin database;
  note GPIO39/40/41 are pin-JTAG (pin-JTAG debug unavailable, USB-JTAG used).
- **RG-6** — external pull-downs on relay driver inputs (GPIO4–7) for the
  power-on → `board_init` high-Z window.

## Consistency check

This table, `board.h`, and `svc100_pin_database_rev_a.csv` were cross-checked and
agree on all 24 assigned pins as of Rev A. Any future GPIO change must update all
three in the same commit.
