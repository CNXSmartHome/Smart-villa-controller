/**
 * @file health.c
 * @brief Boot/runtime health state implementation (see health.h).
 */
#include "health.h"
#include <stdatomic.h>

static const char *TAG = "health";

/* Mandatory checks for the OTA gate (all of HEALTH_CHK_*). */
static atomic_bool s_check[HEALTH_CHK_COUNT];
static atomic_bool s_fault;
static const char *s_fault_reason;

void health_init(void)
{
    for (int i = 0; i < HEALTH_CHK_COUNT; ++i) {
        atomic_store(&s_check[i], false);
    }
    atomic_store(&s_fault, false);
    s_fault_reason = NULL;
    ESP_LOGI(TAG, "health tracking initialized");
}

void health_report(health_check_t check, bool ok)
{
    if (check >= HEALTH_CHK_COUNT) {
        return;
    }
    atomic_store(&s_check[check], ok);
    ESP_LOGI(TAG, "check %d = %s", (int)check, ok ? "ok" : "degraded");
}

void health_latch_fault(const char *reason)
{
    s_fault_reason = reason;           /* set before flag for reader ordering */
    atomic_store(&s_fault, true);
    ESP_LOGE(TAG, "FAULT latched: %s", reason ? reason : "(unspecified)");
}

bool health_ota_gate_ok(void)
{
    if (atomic_load(&s_fault)) {
        return false;
    }
    for (int i = 0; i < HEALTH_CHK_COUNT; ++i) {
        if (!atomic_load(&s_check[i])) {
            return false;
        }
    }
    return true;
}

health_status_t health_status(void)
{
    if (atomic_load(&s_fault)) {
        return HEALTH_FAULT;
    }
    bool all = true, any_degraded = false;
    for (int i = 0; i < HEALTH_CHK_COUNT; ++i) {
        if (!atomic_load(&s_check[i])) {
            all = false;
            /* PRESENCE / NET may legitimately be "degraded but ok" — those
               report ok=true even when degraded, so a false here during the
               boot window means "still booting". */
            (void)any_degraded;
        }
    }
    return all ? HEALTH_OK : HEALTH_BOOTING;
}

void health_get(health_report_t *out)
{
    if (out == NULL) {
        return;
    }
    for (int i = 0; i < HEALTH_CHK_COUNT; ++i) {
        out->check[i] = atomic_load(&s_check[i]);
    }
    out->fault_latched = atomic_load(&s_fault);
    out->fault_reason = out->fault_latched ? s_fault_reason : NULL;
    out->status = health_status();
}

const char *health_status_str(health_status_t s)
{
    switch (s) {
    case HEALTH_BOOTING:  return "booting";
    case HEALTH_OK:       return "ok";
    case HEALTH_DEGRADED: return "degraded";
    case HEALTH_FAULT:    return "fault";
    default:              return "unknown";
    }
}
