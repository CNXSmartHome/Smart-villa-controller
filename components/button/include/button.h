/**
 * @file button.h
 * @brief Config + Reset push-button handler with short/long press detection.
 *
 * Buttons are active-low with internal pull-ups. A periodic, non-blocking poll
 * task debounces and classifies presses, then emits EVT_BUTTON_SHORT /
 * EVT_BUTTON_LONG on the event bus. The control task decides the semantics
 * (e.g. Reset long-press => factory reset).
 */
#ifndef BUTTON_H
#define BUTTON_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Logical button identifiers (carried as event arg0). */
typedef enum {
    BUTTON_CONFIG = 0,
    BUTTON_RESET  = 1,
    BUTTON_COUNT  = 2,
} button_id_t;

/** @brief Timing configuration. */
typedef struct {
    uint16_t debounce_ms;    /**< Stable window to register a press.        */
    uint16_t long_press_ms;  /**< Hold time to classify as a long press.    */
} button_cfg_t;

/**
 * @brief Start the button polling task.
 * @param cfg Timing config (copied). NULL selects sensible defaults.
 * @return SVC_OK or an error from the RTOS layer.
 */
svc_err_t button_start(const button_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */
