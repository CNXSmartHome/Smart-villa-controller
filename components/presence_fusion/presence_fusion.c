/**
 * @file presence_fusion.c
 * @brief 2-sensor presence fusion implementation (see presence_fusion.h).
 */
#include "presence_fusion.h"
#include <stddef.h>   /* NULL */

void presence_fusion_init(presence_fusion_t *f)
{
    if (f == NULL) return;
    f->empty_pending = 0;
    f->empty_since_ms = 0;
    f->fused = PF_UNKNOWN;
}

uint8_t presence_fusion_eval(presence_fusion_t *f, const pf_sensor_in_t *in,
                             uint8_t count, sensor_fault_policy_t policy,
                             uint32_t empty_delay_ms, uint32_t now_ms)
{
    if (f == NULL || in == NULL) {
        return PF_UNKNOWN;
    }
    if (count > PF_MAX_SENSORS) {
        count = PF_MAX_SENSORS;
    }

    bool any_present = false;   /* OR rule */
    bool any_blocking = false;  /* a faulted sensor that prevents "empty" */
    uint8_t considered = 0;     /* enabled sensors that count toward all-empty */
    uint8_t absent_count = 0;

    for (uint8_t i = 0; i < count; ++i) {
        if (!in[i].enabled) {
            continue;
        }
        uint8_t st = in[i].state;

        if (st == PF_UNKNOWN) {
            switch (policy) {
            case PF_FAULT_ASSUME_OCCUPIED:
                any_present = true;          /* fault -> treat as occupied */
                considered++;
                break;
            case PF_FAULT_IGNORE:
                /* excluded entirely */
                break;
            case PF_FAULT_HOLD:
            default:
                any_blocking = true;         /* blocks empty, no occupancy claim */
                considered++;
                break;
            }
            continue;
        }

        considered++;
        if (st == PF_PRESENT) {
            any_present = true;
        } else { /* PF_ABSENT */
            absent_count++;
        }
    }

    /* No usable sensors at all -> unknown (caller's failsafe policy applies). */
    if (considered == 0) {
        f->empty_pending = 0;
        f->fused = PF_UNKNOWN;
        return PF_UNKNOWN;
    }

    if (any_present) {
        /* Occupied wins immediately; cancel any empty countdown. */
        f->empty_pending = 0;
        f->fused = PF_PRESENT;
        return PF_PRESENT;
    }

    /* Not occupied. "Empty" requires EVERY considered sensor to be ABSENT.
       A HOLD-policy fault (any_blocking) keeps the room non-empty so a degraded
       sensor never falsely turns off the load. */
    bool all_absent = (!any_blocking) && (absent_count == considered);

    if (all_absent) {
        if (!f->empty_pending) {
            f->empty_pending = 1;
            f->empty_since_ms = now_ms;
        }
        if ((uint32_t)(now_ms - f->empty_since_ms) >= empty_delay_ms) {
            f->fused = PF_ABSENT;     /* both empty long enough -> room empty */
        } else {
            f->fused = PF_PRESENT;    /* within the delay window: hold occupied */
        }
    } else {
        /* A faulted sensor is blocking empty: hold the room occupied and reset
           the countdown so it must restart once the fault clears. */
        f->empty_pending = 0;
        f->fused = PF_PRESENT;
    }
    return f->fused;
}
