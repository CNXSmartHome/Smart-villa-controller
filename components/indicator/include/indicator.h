/**
 * @file indicator.h
 * @brief Status RGB LED + buzzer service driven by a command queue.
 *
 * Callers never block on hardware: they enqueue a pattern or a beep and a small
 * task renders it. Patterns express system state (boot, ok, fault, config, ota).
 */
#ifndef INDICATOR_H
#define INDICATOR_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Named visual states rendered on the status LED. */
typedef enum {
    IND_OFF = 0,
    IND_BOOT,     /**< steady amber during bring-up        */
    IND_OK,       /**< slow green breathe: running fine     */
    IND_CONFIG,   /**< blue blink: provisioning/AP mode     */
    IND_OTA,      /**< magenta pulse: OTA in progress       */
    IND_FAULT,    /**< red blink: failsafe/critical fault   */
} indicator_pattern_t;

/**
 * @brief Initialize LED + buzzer hardware and start the render task.
 * @return SVC_OK or an error from the LED/PWM driver.
 */
svc_err_t indicator_start(void);

/** @brief Request a new LED pattern (non-blocking). */
svc_err_t indicator_set(indicator_pattern_t pattern);

/**
 * @brief Emit a non-blocking buzzer beep.
 * @param freq_hz   Tone frequency.
 * @param duration_ms Tone length.
 * @param count     Number of beeps.
 */
svc_err_t indicator_beep(uint16_t freq_hz, uint16_t duration_ms, uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* INDICATOR_H */
