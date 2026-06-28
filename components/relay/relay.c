/**
 * @file relay.c
 * @brief Relay output driver implementation (see relay.h).
 */
#include "relay.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "relay";

static relay_cfg_t      s_cfg;
static bool             s_state[BOARD_RELAY_COUNT];   /* logical state */
static SemaphoreHandle_t s_lock;
static bool             s_initialized;

/** Translate logical on/off to the physical GPIO level for a channel. */
static inline uint32_t phys_level(uint8_t ch, bool on)
{
    return (s_cfg.channel[ch].active_high ? on : !on) ? 1U : 0U;
}

static svc_err_t drive_locked(uint8_t ch, bool on)
{
    SVC_RETURN_ON_ERR(gpio_set_level(board_relay_gpio(ch), phys_level(ch, on)));
    s_state[ch] = on;
    return SVC_OK;
}

svc_err_t relay_init(const relay_cfg_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (!s_initialized) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_cfg = *cfg;
    s_initialized = true;

    /* Apply safe state immediately so outputs are deterministic from t=0. */
    SVC_RETURN_ON_ERR(relay_apply_safe());
    ESP_LOGI(TAG, "relay driver ready (%d channels)", BOARD_RELAY_COUNT);
    return SVC_OK;
}

svc_err_t relay_set(uint8_t channel, bool on)
{
    SVC_CHECK_ARG(channel < BOARD_RELAY_COUNT);
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    svc_err_t rc = drive_locked(channel, on);
    xSemaphoreGive(s_lock);
    if (rc == SVC_OK) {
        ESP_LOGD(TAG, "ch%u -> %s", channel, on ? "ON" : "OFF");
    }
    return rc;
}

svc_err_t relay_get(uint8_t channel, bool *out_on)
{
    SVC_CHECK_ARG(channel < BOARD_RELAY_COUNT && out_on != NULL);
    *out_on = s_state[channel];
    return SVC_OK;
}

svc_err_t relay_apply_safe(void)
{
    if (!s_initialized && s_lock == NULL) {
        /* Allowed before mutex exists only during init bootstrap. */
    } else {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    svc_err_t rc = SVC_OK;
    for (uint8_t ch = 0; ch < BOARD_RELAY_COUNT; ++ch) {
        svc_err_t one = gpio_set_level(board_relay_gpio(ch),
                                       phys_level(ch, s_cfg.channel[ch].safe_on));
        s_state[ch] = s_cfg.channel[ch].safe_on;
        if (one != SVC_OK) {
            rc = one;
        }
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
    ESP_LOGW(TAG, "safe state applied (mask=0x%02x)", relay_state_mask());
    return rc;
}

uint8_t relay_state_mask(void)
{
    uint8_t mask = 0;
    for (uint8_t ch = 0; ch < BOARD_RELAY_COUNT; ++ch) {
        if (s_state[ch]) {
            mask |= (uint8_t)(1U << ch);
        }
    }
    return mask;
}
