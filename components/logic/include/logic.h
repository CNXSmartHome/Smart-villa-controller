/**
 * @file logic.h
 * @brief Standalone IF / FOR / THEN rule engine (v0.1) — pure, host-testable.
 *
 * The engine maps a snapshot of inputs (presence + digital inputs) to a desired
 * relay output mask, using a data-driven rule table:
 *
 *     IF   <source> is <active|inactive>          (the condition)
 *     FOR  <for_ms> milliseconds continuously     (debounce / dwell)
 *     THEN drive <relay> <ON|OFF>                 (the action)
 *     with an optional OFF-delay linger when the condition clears.
 *
 * Design notes:
 *  - No ESP-IDF / FreeRTOS dependency. Time is passed in as @p now_ms so the
 *    whole engine can be unit-tested on the host (see tests/host).
 *  - No dynamic allocation. Fixed-size rule table and runtime state.
 *  - Safety arbitration: for any relay, a qualified OFF rule (interlock) beats a
 *    request to turn ON, which beats the default (OFF). De-energize always wins,
 *    and a force-off also CANCELS any pending ON linger on that relay so it can
 *    never re-energize from a stale linger after the interlock clears.
 *  - Fail closed: logic_engine_eval() is safe even if the caller forgot to
 *    validate — an out-of-range source/channel/relay yields "no action" rather
 *    than undefined behavior. Always call logic_ruleset_validate() anyway.
 *
 * Integration status: v0.1 is a reviewed prototype library (Codex findings
 * addressed) and is NOT yet wired into the live control task. Adopting it in
 * control.c is a separate, review-gated step: it changes relay-control behavior
 * and needs a config-schema adapter (svc_rule_t gains `for_ms` and an explicit
 * OFF action in a future schema bump, with migration), not a direct swap.
 */
#ifndef LOGIC_H
#define LOGIC_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGIC_RULE_MAX 16

/* Presence values — MUST match presence_state_t in presence.h. */
#define LOGIC_PRESENCE_UNKNOWN 0
#define LOGIC_PRESENCE_ABSENT  1
#define LOGIC_PRESENCE_PRESENT 2

/** @brief Condition source. */
typedef enum {
    LOGIC_SRC_PRESENCE = 0, /**< mmWave presence state.                  */
    LOGIC_SRC_DININPUT = 1, /**< a debounced digital-input channel.      */
} logic_src_t;

/** @brief Condition polarity (the IF test). */
typedef enum {
    LOGIC_COND_ACTIVE   = 0, /**< present, or input asserted.            */
    LOGIC_COND_INACTIVE = 1, /**< known-absent, or input de-asserted.    */
} logic_cond_t;

/** @brief Action (the THEN). TOGGLE is reserved for v0.2 (not yet supported). */
typedef enum {
    LOGIC_ACT_ON     = 0, /**< drive the target relay ON while qualified. */
    LOGIC_ACT_OFF    = 1, /**< force the target relay OFF (interlock).    */
    LOGIC_ACT_TOGGLE = 2, /**< reserved — rejected by logic_ruleset_validate. */
} logic_action_t;

/** @brief One IF/FOR/THEN rule (data-driven, persistable). */
typedef struct {
    uint8_t  enabled;       /**< 0/1.                                      */
    uint8_t  src;           /**< logic_src_t.                              */
    uint8_t  channel;       /**< digital-input channel (when src==DININPUT)*/
    uint8_t  cond;          /**< logic_cond_t.                             */
    uint16_t for_ms;        /**< condition must hold continuously this long*/
    uint8_t  action;        /**< logic_action_t.                           */
    uint8_t  target_relay;  /**< relay index to drive.                     */
    uint16_t off_delay_s;   /**< linger before releasing an ON action.     */
} logic_rule_t;

/** @brief The rule table. */
typedef struct {
    logic_rule_t rule[LOGIC_RULE_MAX];
    uint8_t      count;     /**< number of valid rules (<= LOGIC_RULE_MAX).*/
} logic_ruleset_t;

/** @brief Input snapshot evaluated by the engine. */
typedef struct {
    uint8_t  presence;      /**< LOGIC_PRESENCE_*.                         */
    uint32_t din_mask;      /**< bit i = debounced digital input i (32-bit). */
} logic_input_t;

/** @brief Per-engine runtime state (caller-owned, zero-initialized). */
typedef struct {
    uint8_t  cond_active[LOGIC_RULE_MAX];    /**< condition currently met (no now==0 ambiguity) */
    uint32_t cond_since_ms[LOGIC_RULE_MAX];  /**< when condition began              */
    uint8_t  qualified[LOGIC_RULE_MAX];      /**< FOR satisfied last eval          */
    uint8_t  linger_active[LOGIC_RULE_MAX];  /**< ON-action linger running          */
    uint32_t linger_until_ms[LOGIC_RULE_MAX];/**< ON-action linger deadline         */
    uint32_t relay_mask;                     /**< last computed desired mask (32-bit) */
} logic_engine_t;

/** @brief Reset engine runtime state. Call once before first eval. */
void logic_engine_init(logic_engine_t *e);

/**
 * @brief Validate a ruleset against the given board limits.
 *
 * Disables (does not delete) any rule that is out of range: unknown source,
 * digital channel >= @p din_count, target relay >= @p relay_count, or a
 * reserved/unsupported action (TOGGLE). Returns SVC_OK; the ruleset is made
 * safe in place. Mirrors the defensive posture of svc_config_sanitize().
 */
svc_err_t logic_ruleset_validate(logic_ruleset_t *rs, uint8_t din_count,
                                 uint8_t relay_count);

/**
 * @brief Evaluate the ruleset against @p in at time @p now_ms.
 *
 * @param e           Engine runtime state.
 * @param rs          Validated ruleset.
 * @param in          Current input snapshot.
 * @param now_ms      Monotonic time in milliseconds.
 * @param relay_count Number of physical relays (output mask width).
 * @param out_mask    Receives the desired relay bitmask (bit i = relay i ON).
 * @return SVC_OK, or SVC_ERR_OUT_OF_RANGE on a bad argument.
 */
svc_err_t logic_engine_eval(logic_engine_t *e, const logic_ruleset_t *rs,
                            const logic_input_t *in, uint32_t now_ms,
                            uint8_t relay_count, uint32_t *out_mask);

#ifdef __cplusplus
}
#endif

#endif /* LOGIC_H */
