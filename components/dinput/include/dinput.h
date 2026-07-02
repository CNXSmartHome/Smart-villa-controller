/**
 * @file dinput.h
 * @brief 8-channel debounced digital input driver.
 *
 * A GPIO change interrupt wakes a periodic debounce evaluation. Only a level
 * that is stable across the debounce window produces a stable-state change and
 * an EVT_DINPUT_EDGE event on the bus. The debounce algorithm itself is pure
 * and host-testable (see dinput_debounce.h).
 */
#ifndef DINPUT_H
#define DINPUT_H

#include "svc_common.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Driver configuration. */
typedef struct {
    uint16_t debounce_ms;    /**< Stable window before a change is accepted.   */
    uint32_t active_low;     /**< Bitmask: bit i set => channel i is active-low.*/
} dinput_cfg_t;

/**
 * @brief Start the digital-input driver and its debounce task.
 *
 * Installs per-pin ISR handlers (assumes the GPIO ISR service is installed by
 * the caller or installs it once), then spawns the debounce task. Emits
 * EVT_DINPUT_EDGE on every accepted transition.
 *
 * @param cfg Driver config (copied). Must be non-NULL.
 * @return SVC_OK or an error from the driver/RTOS layer.
 */
svc_err_t dinput_start(const dinput_cfg_t *cfg);

/**
 * @brief Read the latest debounced logical level of a channel.
 * @param channel Index in [0, BOARD_DINPUT_COUNT).
 * @param out_active Receives true if the input is logically active.
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t dinput_get(uint8_t channel, bool *out_active);

/** @brief Bitmask snapshot of debounced logical-active states. */
uint32_t dinput_state_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* DINPUT_H */
