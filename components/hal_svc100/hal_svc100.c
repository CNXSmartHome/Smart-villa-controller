/**
 * @file hal_svc100.c
 * @brief SVC-100 Rev A HAL target — native ESP32-S3 GPIO (wrapper).
 *
 * Wraps the EXISTING board pin map behind hal_driver_t. No SVC-100 behavior
 * change: relays are native GPIO (active-high), de-energized = low. The dinput
 * active-low handling stays in the dinput service; the HAL returns RAW levels.
 *
 * STATUS: skeleton wrapper — compiles against ESP-IDF, retains today's behavior.
 */
#include "hal_board.h"
#include "svc100_profile.h"
#include "board.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "hal_svc100";

static uint8_t s_relay_shadow;   /* energized bits, 4 relays */

static svc_err_t s_init(void)
{
    return board_init();   /* existing bring-up: relays low, inputs pull-up, I2C */
}

static svc_err_t s_relay_write(uint8_t idx, bool on)
{
    if (idx >= BOARD_RELAY_COUNT) return SVC_ERR_OUT_OF_RANGE;
    /* active-high: logical ON -> level 1. */
    SVC_RETURN_ON_ERR(gpio_set_level(board_relay_gpio(idx), on ? 1 : 0));
    if (on) s_relay_shadow |= (1u << idx); else s_relay_shadow &= ~(1u << idx);
    return SVC_OK;
}

static svc_err_t s_relay_write_mask(uint32_t mask)
{
    svc_err_t rc = SVC_OK;
    for (uint8_t i = 0; i < BOARD_RELAY_COUNT; ++i) {
        svc_err_t one = s_relay_write(i, (mask >> i) & 1u);
        if (one != SVC_OK) rc = one;
    }
    return rc;
}

static svc_err_t s_relay_read(uint8_t idx, bool *on)
{
    if (idx >= BOARD_RELAY_COUNT) return SVC_ERR_OUT_OF_RANGE;
    *on = (s_relay_shadow >> idx) & 1u;
    return SVC_OK;
}

static uint32_t s_din_read_mask(void)
{
    uint32_t m = 0;
    for (uint8_t i = 0; i < BOARD_DINPUT_COUNT; ++i) {
        if (gpio_get_level(board_dinput_gpio(i))) m |= (1u << i);  /* raw level */
    }
    return m;
}

static svc_err_t s_apply_safe_state(void)
{
    svc_err_t rc = SVC_OK;
    for (uint8_t i = 0; i < BOARD_RELAY_COUNT; ++i) {
        svc_err_t one = gpio_set_level(board_relay_gpio(i), 0);  /* de-energized */
        if (one != SVC_OK) rc = one;
    }
    s_relay_shadow = 0;
    return rc;
}

static svc_err_t s_rs485_get_config(uint8_t port, hal_rs485_config_t *out)
{
    if (port == 0) {
        out->uart_port = BOARD_RS485A_UART; out->tx_gpio = BOARD_GPIO_RS485A_TX;
        out->rx_gpio = BOARD_GPIO_RS485A_RX; out->de_gpio = BOARD_GPIO_RS485A_DE;
    } else if (port == 1) {
        out->uart_port = BOARD_RS485B_UART; out->tx_gpio = BOARD_GPIO_RS485B_TX;
        out->rx_gpio = BOARD_GPIO_RS485B_RX; out->de_gpio = BOARD_GPIO_RS485B_DE;
    } else {
        return SVC_ERR_OUT_OF_RANGE;
    }
    out->default_baud = 9600;
    return SVC_OK;
}

static const hal_driver_t s_svc100_driver = {
    .init = s_init,
    .relay_write = s_relay_write,
    .relay_write_mask = s_relay_write_mask,
    .relay_read = s_relay_read,
    .din_read = NULL,
    .din_read_mask = s_din_read_mask,
    .apply_safe_state = s_apply_safe_state,
    .rs485_get_config = s_rs485_get_config,
};

svc_err_t hal_install(void)
{
    (void)TAG;
    s_relay_shadow = 0;
    return hal_board_register(&s_svc100_driver, &SVC100_REVA_PROFILE);
}
