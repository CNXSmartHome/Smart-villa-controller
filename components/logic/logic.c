/**
 * @file logic.c
 * @brief IF/FOR/THEN rule engine implementation (see logic.h).
 *
 * Pure C, no IDF/FreeRTOS. All timing is driven by the caller-supplied now_ms,
 * using unsigned subtraction so it is correct across a 32-bit millisecond wrap
 * for any duration far below 2^32 ms (~49 days).
 */
#include "logic.h"

static const char *TAG = "logic";

void logic_engine_init(logic_engine_t *e)
{
    if (e == NULL) {
        return;
    }
    for (int i = 0; i < LOGIC_RULE_MAX; ++i) {
        e->cond_active[i] = 0;
        e->cond_since_ms[i] = 0;
        e->qualified[i] = 0;
        e->linger_active[i] = 0;
        e->linger_until_ms[i] = 0;
    }
    e->relay_mask = 0;
}

svc_err_t logic_ruleset_validate(logic_ruleset_t *rs, uint8_t din_count,
                                 uint8_t relay_count)
{
    SVC_CHECK_ARG(rs != NULL);
    if (rs->count > LOGIC_RULE_MAX) {
        rs->count = LOGIC_RULE_MAX;
    }
    for (uint8_t i = 0; i < rs->count; ++i) {
        logic_rule_t *r = &rs->rule[i];
        r->enabled = r->enabled ? 1 : 0;
        if (r->src > LOGIC_SRC_DININPUT) {
            r->enabled = 0;
        }
        if (r->cond > LOGIC_COND_INACTIVE) {
            r->enabled = 0;
        }
        /* TOGGLE is reserved for v0.2: disable rather than misbehave. */
        if (r->action != LOGIC_ACT_ON && r->action != LOGIC_ACT_OFF) {
            ESP_LOGW(TAG, "rule %u: unsupported action %u disabled", i, r->action);
            r->enabled = 0;
        }
        if (r->src == LOGIC_SRC_DININPUT && r->channel >= din_count) {
            r->enabled = 0;
        }
        if (r->target_relay >= relay_count) {
            r->enabled = 0;
        }
    }
    return SVC_OK;
}

/** Instantaneous truth of a rule's IF condition against the input snapshot.
 *
 * FAIL CLOSED: this runs at runtime and must be safe even if the caller forgot
 * to validate the ruleset. An unknown source, or a digital channel outside the
 * 8-bit input mask, yields "condition not met" rather than shifting by an
 * out-of-range amount (which is undefined behavior in C). */
static bool condition_met(const logic_rule_t *r, const logic_input_t *in)
{
    bool active;
    if (r->cond != LOGIC_COND_ACTIVE && r->cond != LOGIC_COND_INACTIVE) {
        return false;
    }
    if (r->src == LOGIC_SRC_PRESENCE) {
        if (r->cond == LOGIC_COND_ACTIVE) {
            return in->presence == LOGIC_PRESENCE_PRESENT;
        }
        /* INACTIVE means KNOWN-absent — UNKNOWN satisfies neither (safe). */
        return in->presence == LOGIC_PRESENCE_ABSENT;
    }
    if (r->src != LOGIC_SRC_DININPUT || r->channel >= 8) {
        return false;   /* fail closed: never shift by an out-of-range channel */
    }
    active = (in->din_mask >> r->channel) & 0x1u;
    return (r->cond == LOGIC_COND_ACTIVE) ? active : !active;
}

svc_err_t logic_engine_eval(logic_engine_t *e, const logic_ruleset_t *rs,
                            const logic_input_t *in, uint32_t now_ms,
                            uint8_t relay_count, uint32_t *out_mask)
{
    SVC_CHECK_ARG(e != NULL && rs != NULL && in != NULL && out_mask != NULL);
    SVC_CHECK_ARG(relay_count <= 32);

    /* Per-relay accumulators (up to 32 relays). */
    bool force_off[32]  = {0};
    bool request_on[32] = {0};

    uint8_t count = rs->count <= LOGIC_RULE_MAX ? rs->count : LOGIC_RULE_MAX;
    for (uint8_t i = 0; i < count; ++i) {
        const logic_rule_t *r = &rs->rule[i];
        if (!r->enabled || r->target_relay >= relay_count) {
            e->cond_active[i] = 0;
            e->qualified[i] = 0;
            e->cond_since_ms[i] = 0;
            e->linger_active[i] = 0;
            e->linger_until_ms[i] = 0;
            continue;
        }

        bool met = condition_met(r, in);

        if (met) {
            if (!e->cond_active[i]) {
                e->cond_active[i] = 1;
                e->cond_since_ms[i] = now_ms;   /* explicit flag => now==0 safe */
            }
            bool qualified = (uint32_t)(now_ms - e->cond_since_ms[i]) >= r->for_ms;
            e->qualified[i] = qualified ? 1 : 0;
            if (qualified && r->action == LOGIC_ACT_ON) {
                e->linger_active[i] = 0;   /* active: cancel any linger */
            }
        } else {
            bool was_qualified = e->qualified[i];
            e->cond_active[i] = 0;
            e->qualified[i] = 0;
            if (r->action == LOGIC_ACT_ON && was_qualified) {
                if (r->off_delay_s > 0) {
                    e->linger_active[i] = 1;
                    e->linger_until_ms[i] =
                        now_ms + (uint32_t)r->off_delay_s * 1000u;
                } else {
                    e->linger_active[i] = 0;   /* no linger: release now */
                }
            }
        }

        /* Contributions. */
        if (r->action == LOGIC_ACT_OFF && e->qualified[i]) {
            force_off[r->target_relay] = true;
        }
        if (r->action == LOGIC_ACT_ON) {
            if (e->linger_active[i]) {
                if ((int32_t)(now_ms - e->linger_until_ms[i]) >= 0) {
                    e->linger_active[i] = 0;   /* linger expired */
                }
            }
            if (e->qualified[i] || e->linger_active[i]) {
                request_on[r->target_relay] = true;
            }
        }
    }

    uint32_t mask = 0;
    for (uint8_t r = 0; r < relay_count; ++r) {
        /* Safety: a force-off interlock must also CANCEL any pending ON linger
           targeting this relay. Otherwise the relay would re-energize from a
           stale linger once the interlock clears but before the linger expires. */
        if (force_off[r]) {
            for (uint8_t i = 0; i < count; ++i) {
                if (rs->rule[i].enabled &&
                    rs->rule[i].action == LOGIC_ACT_ON &&
                    rs->rule[i].target_relay == r) {
                    e->linger_active[i] = 0;
                    e->linger_until_ms[i] = 0;
                }
            }
        }
        bool on = force_off[r] ? false : request_on[r];  /* de-energize wins */
        if (on) {
            mask |= (1u << r);   /* 32-bit: relays >= 8 must not be truncated */
        }
    }
    e->relay_mask = mask;
    *out_mask = mask;
    return SVC_OK;
}
