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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "control";

#define CONTROL_TICK_MS 250

static svc_config_t s_cfg;
static volatile bool s_reload;

/* Per-relay linger deadline (0 = no pending off). */
static uint32_t s_off_deadline[BOARD_RELAY_COUNT];
/* Last-known presence/critical-source health. */
static bool s_presence_ok = true;

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

static void evaluate_all_rules(void)
{
    for (int i = 0; i < SVC_RULE_MAX; ++i) {
        apply_rule(&s_cfg.rule[i]);
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

    for (;;) {
        esp_task_wdt_reset();

        svc_event_t ev;
        svc_err_t rc = eventbus_receive(&ev, CONTROL_TICK_MS);

        if (s_reload) {
            s_reload = false;
            ESP_LOGI(TAG, "config reloaded");
        }

        service_off_delays();

        if (rc == SVC_OK) {
            switch (ev.type) {
            case EVT_PRESENCE_CHANGED:
                s_presence_ok = true;
                evaluate_all_rules();
                break;
            case EVT_PRESENCE_STALE:
                /* Critical source lost: failsafe + fault indication. */
                s_presence_ok = false;
                ESP_LOGW(TAG, "presence stale -> failsafe");
                relay_apply_safe();
                break;
            case EVT_DINPUT_EDGE:
                evaluate_all_rules();
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
            evaluate_all_rules();
        }

        update_health_indicator();
    }
}

svc_err_t control_start(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    s_cfg = *cfg;
    memset((void *)s_off_deadline, 0, sizeof(s_off_deadline));

    if (xTaskCreatePinnedToCore(control_task, "control", 4096, NULL, 6,
                                NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "control engine started");
    return SVC_OK;
}

svc_err_t control_reload_config(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    s_cfg = *cfg;     /* struct copy is atomic enough for our fields */
    s_reload = true;
    return SVC_OK;
}
