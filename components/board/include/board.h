/**
 * @file board.h
 * @brief Single source of truth for SVC-100 V1 pin assignments and low-level
 *        hardware bring-up. All other modules consume pins from here so that a
 *        board revision only touches this component.
 *
 * @note  GPIO numbers below are the V1 reference mapping for
 *        ESP32-S3-WROOM-1-N16R8. Verify against the final schematic before
 *        production; they are isolated here precisely so that is a one-file edit.
 */
#ifndef BOARD_H
#define BOARD_H

#include "svc_common.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Channel counts (V1) ----- */
#define BOARD_RELAY_COUNT   4
#define BOARD_DINPUT_COUNT  8
#define BOARD_RS485_COUNT   2

/* ----- Relay outputs (active level configurable in relay module) ----- */
#define BOARD_GPIO_RELAY_0  GPIO_NUM_4
#define BOARD_GPIO_RELAY_1  GPIO_NUM_5
#define BOARD_GPIO_RELAY_2  GPIO_NUM_6
#define BOARD_GPIO_RELAY_3  GPIO_NUM_7

/* ----- Opto-isolated digital inputs (active-low through opto) ----- */
#define BOARD_GPIO_DIN_0    GPIO_NUM_15
#define BOARD_GPIO_DIN_1    GPIO_NUM_16
#define BOARD_GPIO_DIN_2    GPIO_NUM_17
#define BOARD_GPIO_DIN_3    GPIO_NUM_18
#define BOARD_GPIO_DIN_4    GPIO_NUM_8
#define BOARD_GPIO_DIN_5    GPIO_NUM_9
#define BOARD_GPIO_DIN_6    GPIO_NUM_10
#define BOARD_GPIO_DIN_7    GPIO_NUM_11

/* ----- RS485 bus A ----- */
#define BOARD_RS485A_UART   UART_NUM_1
#define BOARD_GPIO_RS485A_TX GPIO_NUM_12
#define BOARD_GPIO_RS485A_RX GPIO_NUM_13
#define BOARD_GPIO_RS485A_DE GPIO_NUM_14   /* DE/RE tied, active-high drives TX */

/* ----- RS485 bus B ----- */
#define BOARD_RS485B_UART   UART_NUM_2
#define BOARD_GPIO_RS485B_TX GPIO_NUM_21
#define BOARD_GPIO_RS485B_RX GPIO_NUM_47
#define BOARD_GPIO_RS485B_DE GPIO_NUM_48

/* ----- I2C (RTC + future expansion) ----- */
#define BOARD_I2C_PORT      I2C_NUM_0
#define BOARD_GPIO_I2C_SDA  GPIO_NUM_1
#define BOARD_GPIO_I2C_SCL  GPIO_NUM_2
#define BOARD_I2C_FREQ_HZ   400000

/* ----- HMI ----- */
#define BOARD_GPIO_LED_STRIP  GPIO_NUM_38  /* WS2812 status LED (RMT)          */
#define BOARD_GPIO_BUZZER     GPIO_NUM_39  /* LEDC PWM buzzer                  */
#define BOARD_GPIO_BTN_CONFIG GPIO_NUM_40  /* active-low, internal pull-up     */
#define BOARD_GPIO_BTN_RESET  GPIO_NUM_41  /* active-low, internal pull-up     */

/** @brief Logical RS485 bus identifiers. */
typedef enum {
    BOARD_RS485_A = 0,
    BOARD_RS485_B = 1,
} board_rs485_id_t;

/**
 * @brief Initialize board-level hardware to a known-safe state.
 *
 * Configures relay GPIOs as outputs driven to the safe (de-energized) level,
 * configures input GPIOs with pull-ups, and brings up the shared I2C master bus.
 * Must be called exactly once, early in app_main, before any driver init.
 *
 * @return SVC_OK on success, or an esp_err_t from the underlying driver.
 */
svc_err_t board_init(void);

/**
 * @brief Get the shared I2C master bus handle (e.g. for the RTC driver).
 * @return Valid handle after board_init(), otherwise NULL.
 */
i2c_master_bus_handle_t board_i2c_bus(void);

/** @brief Map a relay index [0..BOARD_RELAY_COUNT) to its GPIO. */
gpio_num_t board_relay_gpio(uint8_t index);

/** @brief Map a digital-input index [0..BOARD_DINPUT_COUNT) to its GPIO. */
gpio_num_t board_dinput_gpio(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
