/**
 * @file hal_kincony.c
 * @brief KinCony KC868 HAL target — PCF8574 I2C expander IO (SKELETON).
 *
 * Implements hal_driver_t for KinCony boards. The bit/byte mapping is in the
 * pure, host-tested kincony_io_map.c; this file is the ESP-IDF I2C transport +
 * registration. Relay/input IO is over I2C, so EVERY transaction is bounded and
 * an I2C fault returns a FAIL-SAFE error (the caller latches a fault and the
 * control loop applies the safe state).
 *
 * !!! RELEASE GATE RG-K1: PCF8574 reset/default output state, addresses, and
 * relay polarity are UNVERIFIED design assumptions — confirm on a physical
 * KinCony board before trusting any relay (see kincony_io_map.h).
 *
 * STATUS: skeleton — compiles against ESP-IDF but NOT yet bench-validated.
 */
#include "hal_board.h"
#include "kincony_io_map.h"
#include "kincony_profiles.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include <string.h>

static const char *TAG = "hal_kincony";

#define KINCONY_I2C_TIMEOUT_MS 50   /* bounded — relay IO must never hang */

static const board_profile_t *s_profile;
static const kincony_io_map_t *s_io;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_relay_dev[KINCONY_MAX_CHIPS];
static i2c_master_dev_handle_t s_input_dev[KINCONY_MAX_CHIPS];
static uint8_t s_relay_shadow_byte[KINCONY_MAX_CHIPS]; /* last written output bytes */

/** Bounded write of one output byte to a relay expander; fail-safe on error. */
static svc_err_t pcf_write(i2c_master_dev_handle_t dev, uint8_t byte)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = i2c_master_transmit(dev, &byte, 1, KINCONY_I2C_TIMEOUT_MS);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "PCF8574 write failed (0x%x) -> fail-safe", (int)rc);
        return SVC_ERR_BUS_TIMEOUT;   /* fail closed */
    }
    return SVC_OK;
}

/** Bounded read of one input byte; returns SVC_ERR_BUS_TIMEOUT on fault. */
static svc_err_t pcf_read(i2c_master_dev_handle_t dev, uint8_t *byte)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = i2c_master_receive(dev, byte, 1, KINCONY_I2C_TIMEOUT_MS);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "PCF8574 read failed (0x%x)", (int)rc);
        return SVC_ERR_BUS_TIMEOUT;
    }
    return SVC_OK;
}

static svc_err_t k_apply_safe_state(void)
{
    uint8_t safe = kincony_relay_safe_byte(s_io);
    svc_err_t rc = SVC_OK;
    for (uint8_t c = 0; c < s_io->relay_chips; ++c) {
        svc_err_t one = pcf_write(s_relay_dev[c], safe);
        s_relay_shadow_byte[c] = safe;
        if (one != SVC_OK) {
            rc = one;   /* report failure so the caller faults */
        }
    }
    return rc;
}

static svc_err_t k_relay_write_mask(uint32_t mask)
{
    uint8_t bytes[KINCONY_MAX_CHIPS];
    kincony_pack_relays(s_io, mask, bytes);
    svc_err_t rc = SVC_OK;
    for (uint8_t c = 0; c < s_io->relay_chips; ++c) {
        svc_err_t one = pcf_write(s_relay_dev[c], bytes[c]);
        if (one == SVC_OK) {
            s_relay_shadow_byte[c] = bytes[c];
        } else {
            rc = one;
        }
    }
    return rc;
}

static svc_err_t k_relay_write(uint8_t idx, bool on)
{
    uint8_t chip, bit;
    if (!kincony_relay_locate(s_io, idx, &chip, &bit)) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    /* PCF8574 is write-only for outputs: read-modify-write the shadow byte. */
    uint8_t byte = s_relay_shadow_byte[chip];
    bool drive_low = s_io->relay_active_low ? on : !on;  /* energize level */
    if (drive_low) {
        byte &= (uint8_t)~(1u << bit);
    } else {
        byte |= (uint8_t)(1u << bit);
    }
    svc_err_t rc = pcf_write(s_relay_dev[chip], byte);
    if (rc == SVC_OK) {
        s_relay_shadow_byte[chip] = byte;
    }
    return rc;
}

static svc_err_t k_relay_read(uint8_t idx, bool *on)
{
    uint8_t chip, bit;
    if (!kincony_relay_locate(s_io, idx, &chip, &bit)) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    bool low = ((s_relay_shadow_byte[chip] >> bit) & 1u) == 0;
    *on = s_io->relay_active_low ? low : !low;
    return SVC_OK;
}

static uint32_t k_din_read_mask(void)
{
    uint8_t bytes[KINCONY_MAX_CHIPS] = {0};
    for (uint8_t c = 0; c < s_io->input_chips; ++c) {
        uint8_t b = s_io->input_active_low ? 0xFF : 0x00;  /* fail-safe default */
        (void)pcf_read(s_input_dev[c], &b);                /* on fault: idle */
        bytes[c] = b;
    }
    return kincony_unpack_inputs(s_io, bytes);
}

static svc_err_t k_rs485_get_config(uint8_t port, hal_rs485_config_t *out)
{
    if (port != 0) return SVC_ERR_OUT_OF_RANGE;
    /* KC868 RS485 pins — DESIGN ASSUMPTION, verify on board (RG-K1). */
    out->uart_port = 1; out->tx_gpio = 13; out->rx_gpio = 16;
    out->de_gpio = -1; out->default_baud = 9600;
    return SVC_OK;
}

static svc_err_t k_init(void)
{
    /* TODO(kincony): create the I2C master bus + per-PCF8574 device handles for
       s_io->relay_addr / s_io->input_addr, then drive the safe byte. The bus
       creation is omitted in the skeleton; k_apply_safe_state() runs once handles
       exist. Leaving handles NULL makes every IO call fail-safe until wired. */
    ESP_LOGW(TAG, "hal_kincony init: I2C bring-up TODO (skeleton)");
    return SVC_OK;
}

static const hal_driver_t s_kincony_driver = {
    .init = k_init,
    .relay_write = k_relay_write,
    .relay_write_mask = k_relay_write_mask,
    .relay_read = k_relay_read,
    .din_read = NULL,                 /* dispatcher derives single from mask */
    .din_read_mask = k_din_read_mask,
    .apply_safe_state = k_apply_safe_state,
    .rs485_get_config = k_rs485_get_config,
};

/** Installed by app_main for KinCony builds (exactly one hal_*_install linked). */
svc_err_t hal_install(void)
{
#if defined(CONFIG_SVC_TARGET_KINCONY_A16)
    s_profile = &KINCONY_KC868_A16; s_io = &KINCONY_KC868_A16_IO;
#else
    s_profile = &KINCONY_KC868_A8;  s_io = &KINCONY_KC868_A8_IO;
#endif
    for (uint8_t c = 0; c < KINCONY_MAX_CHIPS; ++c) {
        s_relay_shadow_byte[c] = kincony_relay_safe_byte(s_io);
    }
    return hal_board_register(&s_kincony_driver, s_profile);
}
