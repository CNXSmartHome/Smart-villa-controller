/**
 * @file hal_board.c
 * @brief Common HAL dispatcher (see hal_board.h).
 *
 * Pure C (no ESP-IDF): validates indices, keeps the relay shadow mask, and
 * forwards to the registered target driver. Host-unit-tested with a mock driver.
 * Every entry point fails CLOSED when no target is registered.
 */
#include "hal_board.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "hal_board";

static const hal_driver_t   *s_drv;
static const board_profile_t *s_profile;
static atomic_uint           s_relay_shadow;   /* logical energized mask */

svc_err_t hal_board_register(const hal_driver_t *drv, const board_profile_t *profile)
{
    SVC_CHECK_ARG(drv != NULL && profile != NULL);
    SVC_CHECK_ARG(profile->relay_count <= HAL_MAX_RELAY);
    SVC_CHECK_ARG(profile->din_count   <= HAL_MAX_DIN);
    SVC_CHECK_ARG(profile->rs485_count <= HAL_MAX_RS485);
    /* Enforce the project invariant: safe state is always de-energized. */
    SVC_CHECK_ARG(profile->safe_state == SAFE_STATE_DEENERGIZED);
    SVC_CHECK_ARG(drv->relay_write && drv->relay_read && drv->din_read_mask &&
                  drv->apply_safe_state);
    s_drv = drv;
    s_profile = profile;
    atomic_store(&s_relay_shadow, 0);
    return SVC_OK;
}

svc_err_t hal_board_init(void)
{
    if (s_drv == NULL || s_profile == NULL) {
        ESP_LOGE(TAG, "no HAL target registered");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_drv->init) {
        SVC_RETURN_ON_ERR(s_drv->init());
    }
    /* Always come up in the safe state. */
    return hal_board_apply_safe_state();
}

const board_profile_t *hal_board_get_info(void) { return s_profile; }

uint32_t hal_board_get_capabilities(void)
{
    return s_profile ? s_profile->caps : 0u;
}

const board_health_policy_t *hal_board_get_health_policy(void)
{
    return s_profile ? &s_profile->health : NULL;
}

svc_err_t hal_board_apply_safe_state(void)
{
    if (s_drv == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    svc_err_t rc = s_drv->apply_safe_state();
    /* Reflect intent regardless; on failure the caller latches a fault. */
    atomic_store(&s_relay_shadow, 0);
    if (rc != SVC_OK) {
        ESP_LOGE(TAG, "apply_safe_state FAILED (0x%x) — caller must fault", (int)rc);
    }
    return rc;
}

uint8_t hal_relay_count(void) { return s_profile ? s_profile->relay_count : 0; }
uint8_t hal_din_count(void)   { return s_profile ? s_profile->din_count   : 0; }

svc_err_t hal_relay_set(uint8_t index, bool state)
{
    if (s_drv == NULL || s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= s_profile->relay_count) {
        return SVC_ERR_OUT_OF_RANGE;   /* fail closed */
    }
    svc_err_t rc = s_drv->relay_write(index, state);
    if (rc == SVC_OK) {
        if (state) {
            atomic_fetch_or(&s_relay_shadow, (1u << index));
        } else {
            atomic_fetch_and(&s_relay_shadow, ~(1u << index));
        }
    }
    return rc;
}

svc_err_t hal_relay_get(uint8_t index, bool *state)
{
    SVC_CHECK_ARG(state != NULL);
    if (s_drv == NULL || s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= s_profile->relay_count) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    return s_drv->relay_read(index, state);
}

svc_err_t hal_relay_set_mask(uint32_t energized_mask)
{
    if (s_drv == NULL || s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Mask any bits beyond this board's relay count (fail closed on extras). */
    uint32_t valid = (s_profile->relay_count >= 32)
                         ? 0xFFFFFFFFu
                         : ((1u << s_profile->relay_count) - 1u);
    uint32_t m = energized_mask & valid;
    svc_err_t rc;
    if (s_drv->relay_write_mask) {
        rc = s_drv->relay_write_mask(m);
    } else {
        rc = SVC_OK;
        for (uint8_t i = 0; i < s_profile->relay_count; ++i) {
            svc_err_t one = s_drv->relay_write(i, (m >> i) & 1u);
            if (one != SVC_OK) rc = one;
        }
    }
    if (rc == SVC_OK) {
        atomic_store(&s_relay_shadow, m);
    }
    return rc;
}

uint32_t hal_relay_get_mask(void) { return (uint32_t)atomic_load(&s_relay_shadow); }

svc_err_t hal_din_read(uint8_t index, bool *state)
{
    SVC_CHECK_ARG(state != NULL);
    if (s_drv == NULL || s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= s_profile->din_count) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    if (s_drv->din_read) {
        return s_drv->din_read(index, state);
    }
    *state = (s_drv->din_read_mask() >> index) & 1u;
    return SVC_OK;
}

uint32_t hal_din_read_mask(void)
{
    if (s_drv == NULL || s_drv->din_read_mask == NULL) {
        return 0u;
    }
    return s_drv->din_read_mask();
}

svc_err_t hal_rs485_get_config(uint8_t port, hal_rs485_config_t *config)
{
    SVC_CHECK_ARG(config != NULL);
    if (s_drv == NULL || s_profile == NULL || s_drv->rs485_get_config == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (port >= s_profile->rs485_count) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    return s_drv->rs485_get_config(port, config);
}

bool board_health_ota_ok(const board_health_policy_t *policy,
                         bool control_alive, bool presence_ran,
                         bool relay_safe_applied, bool net_settled,
                         bool wdt_stable, bool fault_latched)
{
    if (policy == NULL || fault_latched) {
        return false;                       /* fail closed */
    }
    if (!control_alive || !wdt_stable || !net_settled) {
        return false;
    }
    if (policy->require_relay_safe && !relay_safe_applied) {
        return false;
    }
    /* Presence is mandatory only when the profile says so AND degraded is not
       allowed. KinCony (no mmWave) sets require=false / allow_degraded=true. */
    if (policy->require_presence_modbus && !policy->allow_presence_degraded &&
        !presence_ran) {
        return false;
    }
    return true;
}
