/**
 * @file presence_fusion.h
 * @brief Fuse up to 2 mmWave presence sensors into one room presence state.
 *
 * PURE C (no ESP-IDF): host-unit-tested. Each sensor may be RS485 Modbus, a dry
 * contact input, or disabled. The presence service produces a per-sensor reading
 * (PRESENT / ABSENT / UNKNOWN) and this module fuses them:
 *
 *   - occupied : ANY enabled sensor reports PRESENT (logical OR).
 *   - empty    : ALL enabled sensors report ABSENT, held for room_empty_delay.
 *   - a FAULT/degraded sensor (UNKNOWN) must NOT immediately force "empty" and
 *     turn off AC/light — its effect is governed by ::sensor_fault_policy_t.
 *
 * Time is passed in (now_ms) so the empty-delay timer is testable and wrap-safe.
 */
#ifndef PRESENCE_FUSION_H
#define PRESENCE_FUSION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Presence states — match presence_state_t / LOGIC_PRESENCE_* (0/1/2). */
#define PF_UNKNOWN 0
#define PF_ABSENT  1
#define PF_PRESENT 2

#define PF_MAX_SENSORS 2

/** @brief How a sensor is wired (config field presence_N_type). */
typedef enum {
    PF_SENSOR_DISABLED    = 0,
    PF_SENSOR_RS485       = 1,
    PF_SENSOR_DRY_CONTACT = 2,
} presence_sensor_type_t;

/** @brief What a faulted (UNKNOWN) sensor contributes to the fusion. */
typedef enum {
    /** Hold: a faulted sensor blocks "empty" (so AC/light won't drop) but does
        not assert occupied. Safest default — never falsely turns off. */
    PF_FAULT_HOLD = 0,
    /** A faulted sensor counts as PRESENT (most conservative). */
    PF_FAULT_ASSUME_OCCUPIED = 1,
    /** A faulted sensor is excluded; the other sensor decides. If ALL enabled
        sensors are faulted, the result is UNKNOWN (caller holds last/failsafe
        policy applies elsewhere). */
    PF_FAULT_IGNORE = 2,
} sensor_fault_policy_t;

/** @brief One sensor's current reading for the fusion. */
typedef struct {
    uint8_t enabled;   /**< 0 if PF_SENSOR_DISABLED.                 */
    uint8_t state;     /**< PF_PRESENT / PF_ABSENT / PF_UNKNOWN.     */
} pf_sensor_in_t;

/** @brief Fusion runtime state (caller-owned, zero-initialized). */
typedef struct {
    uint8_t  empty_pending;   /**< empty-delay timer running.        */
    uint32_t empty_since_ms;  /**< when all-empty began.             */
    uint8_t  fused;           /**< last fused output (PF_*).         */
} presence_fusion_t;

/** @brief Reset fusion runtime. Starts at PF_UNKNOWN. */
void presence_fusion_init(presence_fusion_t *f);

/**
 * @brief Fuse the sensor readings at time @p now_ms.
 *
 * @param f             Fusion runtime state.
 * @param in            Per-sensor readings (PF_MAX_SENSORS entries).
 * @param count         Number of sensors actually populated (<= PF_MAX_SENSORS).
 * @param policy        Fault policy for UNKNOWN sensors.
 * @param empty_delay_ms Time all sensors must stay ABSENT before declaring empty.
 * @param now_ms        Monotonic time in ms.
 * @return fused presence PF_PRESENT / PF_ABSENT / PF_UNKNOWN.
 */
uint8_t presence_fusion_eval(presence_fusion_t *f, const pf_sensor_in_t *in,
                             uint8_t count, sensor_fault_policy_t policy,
                             uint32_t empty_delay_ms, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* PRESENCE_FUSION_H */
