/**
 * @file control_logic.h
 * @brief Pure glue between the persisted config schema and the logic engine,
 *        plus the SVC-012 dry-contact presence fallback.
 *
 * No ESP-IDF / FreeRTOS dependency, so it is unit-tested on the host. control.c
 * uses these helpers (behind CONFIG_SVC_USE_LOGIC_ENGINE) to build the engine
 * ruleset from `svc_config_t` and to compute the effective presence signal.
 */
#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include "svc_config.h"
#include "logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Translate one persisted rule into a logic-engine rule.
 *
 * Mapping: trigger_src 0/1 -> presence/dinput; on_active selects the trigger
 * polarity (1 = ACTIVE, 0 = INACTIVE); action 0/1 -> ON/OFF; for_ms and
 * off_delay_s carry through.
 */
void svc_rule_to_logic(const svc_rule_t *src, logic_rule_t *dst);

/**
 * @brief Build a full logic ruleset from a configuration and validate it.
 * @param cfg          Source configuration (already sanitized).
 * @param din_count    Number of digital-input channels (board limit).
 * @param relay_count  Number of relays (board limit).
 * @param out          Destination ruleset.
 */
void logic_ruleset_from_config(const svc_config_t *cfg, uint8_t din_count,
                               uint8_t relay_count, logic_ruleset_t *out);

/**
 * @brief Compute the effective presence signal for the logic engine, applying
 *        the dry-contact fallback when the RS485 sensor is stale (SVC-012).
 *
 * While the RS485 mmWave reading is fresh, it is authoritative. When it is
 * stale and a fallback digital input is configured, the dry-contact (mmWave
 * S1/S2) channel is used: asserted -> PRESENT, de-asserted -> ABSENT. With no
 * fallback, a stale sensor yields UNKNOWN (the control engine independently
 * applies failsafe on stale).
 *
 * @param rs485_state    Latest RS485 presence (LOGIC_PRESENCE_*).
 * @param rs485_stale    True if the RS485 sensor poll has gone stale.
 * @param din_mask       Debounced digital-input logical-active mask.
 * @param fallback_en    cfg->fallback_din_enabled.
 * @param fallback_chan  cfg->fallback_din_chan.
 * @return LOGIC_PRESENCE_UNKNOWN / _ABSENT / _PRESENT.
 */
uint8_t presence_effective(uint8_t rs485_state, bool rs485_stale,
                           uint32_t din_mask, bool fallback_en,
                           uint8_t fallback_chan);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_LOGIC_H */
