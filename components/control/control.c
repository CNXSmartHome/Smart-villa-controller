/**
 * @file control.c
 * @brief Standalone control engine implementation (see control.h).
 *
 * The task blocks on the event bus with a bounded timeout. On each wake it:
 *   1. feeds the Task Watchdog,
 *   2. applies any expired relay off-delays (linger timers),
 *   3. processes the dequeued event (presence/input/button/net),
 *   4. re-evaluates rules and drives relays + the indicator.
 * No blocking calls, no allocation after start.
 */
#include "control.h"
#include "eventbus.h"
#include "relay.h"
#include "indicator.h"
#include "presence.h"
#include "dinput.h"
#include "storage.h"
#include "netmgr.h"
#include "health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "control";

#define CONTROL_TICK_MS    250
#define CONTROL_CFG_LOCK_MS 100

/* Shared configuration, written by control_reload_config(), read by the task.
   ALL access is serialized by s_cfg_lock; the task never evaluates rules against
   s_cfg directly — it works from a local snapshot taken under the lock. */
static svc_config_t      s_cfg;
static SemaphoreHandle_t s_cfg_lock;
static volatile bool     s_reload;

/* Per-relay linger deadline (0 = no pending off). */
static uint32_t s_off_deadline[BOARD_RELAY_COUNT];
/* Last-known presence/critical-source health. */
static bool s_presence_ok = true;

/**
 * @brief Copy the shared config into @p snap under the lock (bounded wait).
 * @return true if the snapshot was refreshed, false if the lock was unavailable
 *         (caller should keep using its previous snapshot).
 */
static bool cfg_snapshot(svc_config_t *snap)
{
    if (s_cfg_lock == NULL) {
        *snap = s_cfg;            /* pre-task bootstrap only */
        return true;
    }
    if (xSemaphoreTake(s_cfg_lock, pdMS_TO_TICKS(CONTROL_CFG_LOCK_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "cfg snapshot: lock busy, reusing previous");
        return false;
    }
    *snap = s_cfg;
    xSemaphoreGive(s_cfg_lock);
    return true;
}

/** Decide whether a rule's trigger is currently "active". */
static bool rule_trigger_active(const svc_rule_t *r)
{
    if (r->trigger_src == 0) {              /* presence */
        return presence_get() == PRESENCE_PRESENT;
    }
    bool active = false;                    /* digital input */
    if (dinput_get(r->trigger_chan, &active) != SVC_OK) {
        return false;
    }
    return active;
}

/** Apply one rule to its target relay, honoring the off-delay. */
static void apply_rule(const svc_rule_t *r)
{
    if (!r->enabled || r->target_relay >= BOARD_RELAY_COUNT) {
        return;
    }
    bool active = rule_trigger_active(r);
    bool want_on = active ? (r->on_active != 0) : (r->on_active == 0);

    if (want_on) {
        s_off_deadline[r->target_relay] = 0;     /* cancel any pending off */
        relay_set(r->target_relay, true);
    } else if (r->off_delay_s > 0) {
        if (s_off_deadline[r->target_relay] == 0) {
            s_off_deadline[r->target_relay] =
                svc_now_ms() + (uint32_t)r->off_delay_s * 1000U;
        }
    } else {
        relay_set(r->target_relay, false);
    }
}

/** Turn off relays whose linger timer has expired. */
static void service_off_delays(void)
{
    uint32_t now = svc_now_ms();
    for (uint8_t i = 0; i < BOARD_RELAY_COUNT; ++i) {
        if (s_off_deadline[i] != 0 && (int32_t)(now - s_off_deadline[i]) >= 0) {
            s_off_deadline[i] = 0;
            relay_set(i, false);
        }
    }
}

/** Evaluate every rule from a stable, lock-free local snapshot. */
static void evaluate_all_rules(const svc_config_t *snap)
{
    for (int i = 0; i < SVC_RULE_MAX; ++i) {
        apply_rule(&snap->rule[i]);
    }
}

static void handle_button(const svc_event_t *ev)
{
    bool is_long = (ev->type == EVT_BUTTON_LONG);
    if (ev->arg0 == 1 /* reset */ && is_long) {
        ESP_LOGW(TAG, "factory reset requested");
        indicator_set(IND_FAULT);
        indicator_beep(1200, 120, 3);
        (void)storage_factory_reset();
        vTaskDelay(pdMS_TO_TICKS(400));
        esp_restart();
    } else if (ev->arg0 == 0 /* config */ && is_long) {
        ESP_LOGI(TAG, "entering provisioning");
        indicator_set(IND_CONFIG);
        (void)netmgr_enter_provisioning();
    } else {
        indicator_beep(2000, 60, 1);
    }
}

static void update_health_indicator(void)
{
    if (!s_presence_ok) {
        indicator_set(IND_FAULT);
    } else {
        indicator_set(IND_OK);
    }
}

static void control_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));   /* subscribe this task */
    indicator_set(IND_OK);

    /* Private working snapshot of the configuration; refreshed only under lock. */
    svc_config_t snap;
    (void)cfg_snapshot(&snap);

    /* Control loop is alive and watchdog-fed: report readiness for the OTA gate. */
    health_report(HEALTH_CHK_CONTROL_ALIVE, true);

    for (;;) {
        esp_task_wdt_reset();

        svc_event_t ev;
        svc_err_t rc = eventbus_receive(&ev, CONTROL_TICK_MS);

        if (s_reload) {
            if (cfg_snapshot(&snap)) {     /* only clear once we actually copied */
                s_reload = false;
                ESP_LOGI(TAG, "config reloaded");
            }
        }

        service_off_delays();

        if (rc == SVC_OK) {
            switch (ev.type) {
            case EVT_PRESENCE_CHANGED:
                s_presence_ok = true;
                evaluate_all_rules(&snap);
                break;
            case EVT_PRESENCE_STALE:
                /* Critical source lost: failsafe + fault indication. */
                s_presence_ok = false;
                ESP_LOGW(TAG, "presence stale -> failsafe");
                relay_apply_safe();
                break;
            case EVT_DINPUT_EDGE:
                evaluate_all_rules(&snap);
                break;
            case EVT_BUTTON_SHORT:
            case EVT_BUTTON_LONG:
                handle_button(&ev);
                break;
            case EVT_NET_UP:
            case EVT_NET_DOWN:
            case EVT_CONFIG_CHANGED:
                /* informational for the engine; rules unaffected */
                break;
            default:
                break;
            }
        } else {
            /* Timeout: periodic re-evaluation keeps outputs correct even if an
               event was ever dropped, and recovers presence health. */
            if (!s_presence_ok && presence_get() == PRESENCE_PRESENT) {
                s_presence_ok = true;
            }
            evaluate_all_rules(&snap);
        }

        update_health_indicator();
    }
}

svc_err_t control_start(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (s_cfg_lock == NULL) {
        s_cfg_lock = xSemaphoreCreateMutex();
        if (s_cfg_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    /* Safe to write without the lock here: the task does not exist yet. */
    s_cfg = *cfg;
    memset((void *)s_off_deadline, 0, sizeof(s_off_deadline));

    if (xTaskCreatePinnedToCore(control_task, "control", 4096, NULL, 6,
                                NULL, 1) != pdPASS) {
        /* Task did not start: release the mutex and reset state so a retry
           starts cleanly and no resource is leaked. */
        vSemaphoreDelete(s_cfg_lock);
        s_cfg_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "control engine started");
    return SVC_OK;
}

svc_err_t control_reload_config(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (s_cfg_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Publish the new config atomically w.r.t. the control task's snapshot. */
    if (xSemaphoreTake(s_cfg_lock, pdMS_TO_TICKS(CONTROL_CFG_LOCK_MS)) != pdTRUE) {
        return SVC_ERR_BUS_TIMEOUT;
    }
    s_cfg = *cfg;
    xSemaphoreGive(s_cfg_lock);
    s_reload = true;
    return SVC_OK;
}
