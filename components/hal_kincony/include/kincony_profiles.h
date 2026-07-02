/**
 * @file kincony_profiles.h
 * @brief Board profiles for KinCony KC868-A8 / KC868-A16 (pure data).
 *
 * Placeholder profiles with real-design assumptions. The PCF8574 addresses and
 * relay polarity are UNVERIFIED — see RG-K1 in kincony_io_map.h.
 */
#ifndef KINCONY_PROFILES_H
#define KINCONY_PROFILES_H

#include "hal_board.h"
#include "kincony_io_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- KC868-A8: 8 relays / 8 inputs / 1 RS485 ---- */
static const char *const KC868_A8_RELAY_LABELS[8] = {
    "Relay 1","Relay 2","Relay 3","Relay 4","Relay 5","Relay 6","Relay 7","Relay 8" };
static const char *const KC868_A8_DIN_LABELS[8] = {
    "Input 1","Input 2","Input 3","Input 4","Input 5","Input 6","Input 7","Input 8" };

static const board_profile_t KINCONY_KC868_A8 = {
    .board_id = "kincony_kc868_a8",
    .board_name = "KinCony KC868-A8",
    .relay_count = 8, .din_count = 8, .rs485_count = 1,
    .has_presence_modbus = false,
    .has_physical_provision_button = false,   /* MVP: provision via AP mode */
    .relay_labels = KC868_A8_RELAY_LABELS,
    .din_labels = KC868_A8_DIN_LABELS,
    .relay_polarity = RELAY_ACTIVE_LOW,        /* ASSUMPTION — RG-K1 */
    .safe_state = SAFE_STATE_DEENERGIZED,
    .health = { .require_presence_modbus = false, .allow_presence_degraded = true,
                .require_relay_safe = true, .boot_window_ms = 30000 },
    .caps = HAL_CAP_IO_EXPANDER | HAL_CAP_BUZZER,
};

/* PCF8574 addresses are DESIGN ASSUMPTIONS (RG-K1: verify on real board). */
static const kincony_io_map_t KINCONY_KC868_A8_IO = {
    .relay_addr = {0x24}, .relay_chips = 1,
    .input_addr = {0x22}, .input_chips = 1,
    .relay_active_low = true, .input_active_low = true,
};

/* ---- KC868-A16: 16 relays / 16 inputs / 1 RS485 ---- */
static const board_profile_t KINCONY_KC868_A16 = {
    .board_id = "kincony_kc868_a16",
    .board_name = "KinCony KC868-A16",
    .relay_count = 16, .din_count = 16, .rs485_count = 1,
    .has_presence_modbus = false,
    .has_physical_provision_button = false,
    .relay_labels = NULL, .din_labels = NULL,   /* generic Relay N / Input N */
    .relay_polarity = RELAY_ACTIVE_LOW,          /* ASSUMPTION — RG-K1 */
    .safe_state = SAFE_STATE_DEENERGIZED,
    .health = { .require_presence_modbus = false, .allow_presence_degraded = true,
                .require_relay_safe = true, .boot_window_ms = 30000 },
    .caps = HAL_CAP_IO_EXPANDER | HAL_CAP_BUZZER,
};

static const kincony_io_map_t KINCONY_KC868_A16_IO = {
    .relay_addr = {0x24, 0x25}, .relay_chips = 2,
    .input_addr = {0x21, 0x22}, .input_chips = 2,
    .relay_active_low = true, .input_active_low = true,
};

#ifdef __cplusplus
}
#endif

#endif /* KINCONY_PROFILES_H */
