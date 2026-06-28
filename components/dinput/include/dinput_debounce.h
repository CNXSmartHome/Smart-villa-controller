/**
 * @file dinput_debounce.h
 * @brief Pure integrator-based debounce logic (no IDF/HW dependencies).
 *
 * Each channel runs a saturating counter. A consistent raw level over enough
 * ticks moves the counter to a rail and flips the stable output. This file is
 * compiled both on-target and on host for unit tests.
 */
#ifndef DINPUT_DEBOUNCE_H
#define DINPUT_DEBOUNCE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t counter;   /**< Integrator value, 0..max.        */
    uint8_t max;       /**< Saturation rail (ticks to flip). */
    bool    stable;    /**< Current debounced output.        */
} dinput_debounce_t;

/** @brief Initialize a debounce channel with @p ticks to accept a change. */
static inline void dinput_debounce_init(dinput_debounce_t *d, uint8_t ticks,
                                        bool initial)
{
    d->max = ticks ? ticks : 1;
    d->counter = initial ? d->max : 0;
    d->stable = initial;
}

/**
 * @brief Advance the integrator by one tick with @p raw sample.
 * @return true if the stable output changed on this tick.
 */
static inline bool dinput_debounce_update(dinput_debounce_t *d, bool raw)
{
    if (raw && d->counter < d->max) {
        d->counter++;
    } else if (!raw && d->counter > 0) {
        d->counter--;
    }
    if (!d->stable && d->counter >= d->max) {
        d->stable = true;
        return true;
    }
    if (d->stable && d->counter == 0) {
        d->stable = false;
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif /* DINPUT_DEBOUNCE_H */
