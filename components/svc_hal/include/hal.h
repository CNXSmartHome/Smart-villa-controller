/**
 * @file hal.h
 * @brief Smart Villa OS Core — HAL public entry point (compatibility redirect).
 *
 * The HAL contract is defined ONCE in hal_board.h (the implemented, host-tested
 * dispatcher: 32-bit masks, HAL_MAX_RELAY/HAL_MAX_DIN = 32, hal_relay_count(),
 * hal_din_count(), board_profile_t, hal_board_register(), board_health_ota_ok()).
 *
 * This header used to carry an earlier "proposed v0" interface with DIFFERENT
 * maxima (16) and DIFFERENT symbol names (hal_relay_get_count / hal_di_get_count).
 * That duplicate diverged from the implementation and is removed to prevent macro
 * conflicts and stale prototypes. Include either header — both now resolve to the
 * single hal_board.h contract. New code should include "hal_board.h" directly.
 */
#ifndef HAL_H
#define HAL_H

#include "hal_board.h"

#endif /* HAL_H */
