/**
 * @file svc100_profile.h
 * @brief Board profile for SVC-100 Pro Rev A (pure data).
 */
#ifndef SVC100_PROFILE_H
#define SVC100_PROFILE_H

#include "hal_board.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *const SVC100_RELAY_LABELS[4] = {
    "Relay 1","Relay 2","Relay 3","Relay 4" };
static const char *const SVC100_DIN_LABELS[8] = {
    "DIN 1","DIN 2","DIN 3","DIN 4","DIN 5","DIN 6","DIN 7","DIN 8" };

static const board_profile_t SVC100_REVA_PROFILE = {
    .board_id = "svc100_reva",
    .board_name = "SVC-100 Pro Rev A",
    .relay_count = 4, .din_count = 8, .rs485_count = 2,
    .has_presence_modbus = true,
    .has_physical_provision_button = true,
    .relay_labels = SVC100_RELAY_LABELS,
    .din_labels = SVC100_DIN_LABELS,
    .relay_polarity = RELAY_ACTIVE_HIGH,
    .safe_state = SAFE_STATE_DEENERGIZED,
    .health = { .require_presence_modbus = true, .allow_presence_degraded = false,
                .require_relay_safe = true, .boot_window_ms = 30000 },
    .caps = HAL_CAP_RTC | HAL_CAP_BUZZER | HAL_CAP_RGB_LED | HAL_CAP_NATIVE_USB |
            HAL_CAP_PRESENCE_485 | HAL_CAP_PROV_BUTTON,
};

#ifdef __cplusplus
}
#endif

#endif /* SVC100_PROFILE_H */
