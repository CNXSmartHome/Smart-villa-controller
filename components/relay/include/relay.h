/**
 * @file relay.h
 * @brief 4-channel dry-contact relay output driver.
 *
 * Tracks logical channel state, applies a per-channel active-high/active-low
 * polarity, and provides an atomic safe-state application used at boot and on
 * failsafe. No blocking, no allocation after init.
 */
#ifndef RELAY_H
#define RELAY_H

#include "svc_common.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Per-channel configuration. */
typedef struct {
    bool active_high;   /**< true: logic-1 energizes; false: inverted output.  */
    bool safe_on;       /**< Desired energized/de-energized state on failsafe. */
} relay_channel_cfg_t;

/** @brief Driver configuration (one entry per physical relay). */
typedef struct {
    relay_channel_cfg_t channel[BOARD_RELAY_COUNT];
} relay_cfg_t;

/**
 * @brief Initialize the relay driver and drive all channels to their safe state.
 * @param cfg Polarity/safe-state config (copied internally). Must be non-NULL.
 * @return SVC_OK or an error from the GPIO layer.
 */
svc_err_t relay_init(const relay_cfg_t *cfg);

/**
 * @brief Set one relay channel.
 * @param channel Index in [0, BOARD_RELAY_COUNT).
 * @param on      true = energized (logical), polarity applied internally.
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t relay_set(uint8_t channel, bool on);

/**
 * @brief Read back the last commanded logical state of a channel.
 * @param channel Index in [0, BOARD_RELAY_COUNT).
 * @param out_on  Receives the logical state.
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t relay_get(uint8_t channel, bool *out_on);

/** @brief Drive every channel to its configured safe state (failsafe entry). */
svc_err_t relay_apply_safe(void);

/** @brief Bitmask snapshot of current logical states (bit i = channel i). */
uint8_t relay_state_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_H */
