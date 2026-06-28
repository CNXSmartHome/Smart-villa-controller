/**
 * @file health.h
 * @brief Boot/runtime health state used to gate OTA image validation and to
 *        report device health over the Web UI.
 *
 * Modules report individual readiness signals as they come up. The OTA image is
 * marked valid (cancelling bootloader rollback) ONLY when every mandatory check
 * has passed and no fault is latched — never on a fixed timer.
 *
 * All setters are safe to call from any task context; state is held in
 * lock-free atomics suitable for single-writer-per-flag, multi-reader use.
 */
#ifndef HEALTH_H
#define HEALTH_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Overall device health, derived from the individual checks. */
typedef enum {
    HEALTH_BOOTING = 0, /**< Bring-up not finished.                          */
    HEALTH_OK,          /**< All mandatory checks passed.                    */
    HEALTH_DEGRADED,    /**< Running, but a recoverable check failed cleanly. */
    HEALTH_FAULT,       /**< A critical fault is latched.                    */
} health_status_t;

/** @brief Individual readiness signals (one writer each). */
typedef enum {
    HEALTH_CHK_CONTROL_ALIVE = 0, /**< Control task entered its loop.         */
    HEALTH_CHK_PRESENCE_RAN,      /**< Presence polled once OR degraded clean. */
    HEALTH_CHK_RELAY_SAFE,        /**< Relay safe state has been applied.      */
    HEALTH_CHK_NET_SETTLED,       /**< Net stack up OR failed safely.          */
    HEALTH_CHK_WDT_STABLE,        /**< Watchdog stable across boot window.     */
    HEALTH_CHK_COUNT,
} health_check_t;

/** @brief Snapshot of all checks + derived status for reporting. */
typedef struct {
    bool            check[HEALTH_CHK_COUNT];
    bool            fault_latched;
    health_status_t status;
    const char     *fault_reason; /**< NULL unless a fault is latched. */
} health_report_t;

/** @brief Reset all checks to "not ready" and clear faults. Call once at boot. */
void health_init(void);

/** @brief Report a check result. @p ok=false on a check marks it degraded. */
void health_report(health_check_t check, bool ok);

/**
 * @brief Latch a critical fault (sticky until reboot).
 * @param reason Static string describing the fault (stored by pointer).
 */
void health_latch_fault(const char *reason);

/**
 * @brief OTA validation gate.
 * @return true only if every mandatory check passed and no fault is latched.
 */
bool health_ota_gate_ok(void);

/** @brief Current derived status. */
health_status_t health_status(void);

/** @brief Fill @p out with a full snapshot for reporting (e.g. /api/status). */
void health_get(health_report_t *out);

/** @brief Short, stable string for a status (for JSON). */
const char *health_status_str(health_status_t s);

#ifdef __cplusplus
}
#endif

#endif /* HEALTH_H */
