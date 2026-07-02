/**
 * @file relay.c
 * @brief Relay output driver implementation (see relay.h).
 *
 * Logical channel state lives in a single atomic bitmask so reads
 * (relay_get / relay_state_mask) are lock-free and never tear. The GPIO write
 * plus its corresponding mask update are serialized by a mutex acquired with a
 * BOUNDED timeout — relay control must never block forever (P5). The failsafe
 * path applies the safe state best-effort even if the lock cannot be taken.
 */
#include "relay.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdatomic.h>

static const char *TAG = "relay";

/* Max time any caller will wait for the relay mutex before failing explicitly. */
#define RELAY_LOCK_TIMEOUT_MS 200

static relay_cfg_t       s_cfg;
static atomic_uint       s_state_mask;   /* bit i = logical state of channel i */
static SemaphoreHandle_t s_lock;
static bool              s_initialized;

/** Translate logical on/off to the physical GPIO level for a channel. */
static inline uint32_t phys_level(uint8_t ch, bool on)
{
    return (s_cfg.channel[ch].active_high ? on : !on) ? 1U : 0U;
}

static inline void mask_set_bit(uint8_t ch, bool on)
{
    if (on) {
        atomic_fetch_or(&s_state_mask, (uint32_t)(1U << ch));
    } else {
        atomic_fetch_and(&s_state_mask, (uint32_t)~(1U << ch));
    }
}

static inline bool mask_get_bit(uint8_t ch)
{
    return (atomic_load(&s_state_mask) >> ch) & 0x1u;
}

static svc_err_t drive(uint8_t ch, bool on)
{
    SVC_RETURN_ON_ERR(gpio_set_level(board_relay_gpio(ch), phys_level(ch, on)));
    mask_set_bit(ch, on);
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

    /* Apply safe state immediately so outputs are deterministic from t=0. If
       that fails, fully unwind: delete the mutex and clear the initialized flag
       so the driver does not present a half-configured state to callers. */
    svc_err_t rc = relay_apply_safe();
    if (rc != SVC_OK) {
        ESP_LOGE(TAG, "safe-state apply failed (0x%x); unwinding init", (int)rc);
        if (s_lock != NULL) {
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
        }
        s_initialized = false;
        return rc;
    }
    ESP_LOGI(TAG, "relay driver ready (%d channels)", BOARD_RELAY_COUNT);
    return SVC_OK;
}

svc_err_t relay_set(uint8_t channel, bool on)
{
    SVC_CHECK_ARG(channel < BOARD_RELAY_COUNT);
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(RELAY_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "ch%u set: lock timeout", channel);
        return SVC_ERR_BUS_TIMEOUT;   /* never hang the caller */
    }
    svc_err_t rc = drive(channel, on);
    xSemaphoreGive(s_lock);
    if (rc == SVC_OK) {
        ESP_LOGD(TAG, "ch%u -> %s", channel, on ? "ON" : "OFF");
    }
    return rc;
}

svc_err_t relay_get(uint8_t channel, bool *out_on)
{
    SVC_CHECK_ARG(channel < BOARD_RELAY_COUNT && out_on != NULL);
    *out_on = mask_get_bit(channel);     /* lock-free atomic read */
    return SVC_OK;
}

svc_err_t relay_apply_safe(void)
{
    /* Failsafe must run even under contention. Try to take the lock with a
       bound; if it cannot be acquired, force the safe state anyway (best
       effort) — a safe output takes precedence over strict serialization. */
    bool locked = false;
    if (s_lock != NULL) {
        locked = (xSemaphoreTake(s_lock, pdMS_TO_TICKS(RELAY_LOCK_TIMEOUT_MS))
                  == pdTRUE);
        if (!locked) {
            ESP_LOGW(TAG, "safe-state: lock busy, forcing outputs anyway");
        }
    }
    svc_err_t rc = SVC_OK;
    for (uint8_t ch = 0; ch < BOARD_RELAY_COUNT; ++ch) {
        bool safe_on = s_cfg.channel[ch].safe_on;
        svc_err_t one = gpio_set_level(board_relay_gpio(ch),
                                       phys_level(ch, safe_on));
        mask_set_bit(ch, safe_on);
        if (one != SVC_OK) {
            rc = one;
        }
    }
    if (locked) {
        xSemaphoreGive(s_lock);
    }
    ESP_LOGW(TAG, "safe state applied (mask=0x%08" PRIx32 ")", relay_state_mask());
    return rc;
}

uint32_t relay_state_mask(void)
{
    return (uint32_t)atomic_load(&s_state_mask);
}
