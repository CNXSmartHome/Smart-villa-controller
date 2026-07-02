/**
 * @file control_logic.c
 * @brief Config<->logic adapter + dry-contact presence fallback (see header).
 *
 * Pure C: no ESP-IDF/FreeRTOS. Unit-tested on the host.
 */
#include "control_logic.h"

void svc_rule_to_logic(const svc_rule_t *src, logic_rule_t *dst)
{
    dst->enabled = src->enabled ? 1 : 0;
    dst->src     = (src->trigger_src == 1) ? LOGIC_SRC_DININPUT
                                           : LOGIC_SRC_PRESENCE;
    dst->channel = src->trigger_chan;
    /* on_active selects which polarity is the trigger. */
    dst->cond    = src->on_active ? LOGIC_COND_ACTIVE : LOGIC_COND_INACTIVE;
    dst->for_ms  = src->for_ms;
    dst->action  = (src->action == SVC_RULE_ACTION_OFF) ? LOGIC_ACT_OFF
                                                        : LOGIC_ACT_ON;
    dst->target_relay = src->target_relay;
    dst->off_delay_s  = src->off_delay_s;
}

void logic_ruleset_from_config(const svc_config_t *cfg, uint8_t din_count,
                               uint8_t relay_count, logic_ruleset_t *out)
{
    uint8_t n = SVC_RULE_MAX < LOGIC_RULE_MAX ? SVC_RULE_MAX : LOGIC_RULE_MAX;
    for (uint8_t i = 0; i < n; ++i) {
        svc_rule_to_logic(&cfg->rule[i], &out->rule[i]);
    }
    out->count = n;
    /* Defence in depth: validate against board limits even though the config
       was already sanitized (fail closed at the engine boundary too). */
    logic_ruleset_validate(out, din_count, relay_count);
}

uint8_t presence_effective(uint8_t rs485_state, bool rs485_stale,
                           uint32_t din_mask, bool fallback_en,
                           uint8_t fallback_chan)
{
    if (!rs485_stale) {
        return rs485_state;   /* RS485 is authoritative while fresh */
    }
    if (fallback_en && fallback_chan < 32) {
        bool active = (din_mask >> fallback_chan) & 0x1u;
        return active ? LOGIC_PRESENCE_PRESENT : LOGIC_PRESENCE_ABSENT;
    }
    return LOGIC_PRESENCE_UNKNOWN;
}
