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
#include "control_logic.h"
#include "logic.h"
#include "sdkconfig.h"
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

#if !CONFIG_SVC_USE_LOGIC_ENGINE
/* Per-relay linger deadline (0 = no pending off) — legacy evaluator only.
   The logic engine manages its own dwell/linger internally. */
static uint32_t s_off_deadline[BOARD_RELAY_COUNT];
#endif
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

#if !CONFIG_SVC_USE_LOGIC_ENGINE
/** Decide whether a rule's trigger is currently "active" (legacy evaluator). */
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

/** Apply one rule to its target relay, honoring the off-delay (legacy). */
static void apply_rule(const svc_rule_t *r)
{
    if (!r->enabled || r->target_relay >= BOARD_RELAY_COUNT) {
        return;
    }
    bool active = rule_trigger_active(r);
    bool triggered = active ? (r->on_active != 0) : (r->on_active == 0);

    /* Schema v3 safety interlocks must fail safe even while the review-gated
       logic engine is disabled. The legacy evaluator does not implement FOR
       dwell, but it must never reinterpret an OFF rule as an ON request. */
    if (r->action == SVC_RULE_ACTION_OFF) {
        if (triggered) {
            s_off_deadline[r->target_relay] = 0;
            relay_set(r->target_relay, false);
        }
        return;
    }

    if (triggered) {
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

/** Turn off relays whose linger timer has expired (legacy evaluator). */
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
#endif /* !CONFIG_SVC_USE_LOGIC_ENGINE */

#if CONFIG_SVC_USE_LOGIC_ENGINE
/* ---- Review-gated logic-engine path (CONFIG_SVC_USE_LOGIC_ENGINE) ----
   Disabled by default; the legacy evaluator below remains the production path
   until this is signed off on the bench. */
static logic_engine_t  s_engine;
static logic_ruleset_t s_ruleset;
static bool            s_ruleset_dirty = true;

/** Rebuild the engine ruleset from the snapshot when the config changed. */
static void logic_refresh_ruleset(const svc_config_t *snap)
{
    logic_ruleset_from_config(snap, BOARD_DINPUT_COUNT, BOARD_RELAY_COUNT,
                              &s_ruleset);
    logic_engine_init(&s_engine);
    s_ruleset_dirty = false;
}

/** Evaluate the logic engine and drive every relay to the desired state. */
static void evaluate_all_rules(const svc_config_t *snap)
{
    if (s_ruleset_dirty) {
        logic_refresh_ruleset(snap);
    }
    /* Effective presence: RS485 while fresh, else dry-contact fallback (SVC-012).
       s_presence_ok is false once the RS485 sensor has gone stale. */
    uint8_t eff = presence_effective((uint8_t)presence_get(), !s_presence_ok,
                                     dinput_state_mask(),
                                     snap->fallback_din_enabled,
                                     snap->fallback_din_chan);
    logic_input_t in = { .presence = eff, .din_mask = dinput_state_mask() };
    uint32_t want = 0;
    if (logic_engine_eval(&s_engine, &s_ruleset, &in, svc_now_ms(),
                          BOARD_RELAY_COUNT, &want) != SVC_OK) {
        return;
    }
    for (uint8_t r = 0; r < BOARD_RELAY_COUNT; ++r) {
        bool on = (want >> r) & 0x1u;
        bool cur = false;
        if (relay_get(r, &cur) == SVC_OK && cur != on) {
            relay_set(r, on);
        }
    }
}
#else
/** Evaluate every rule from a stable, lock-free local snapshot (legacy). */
static void evaluate_all_rules(const svc_config_t *snap)
{
    /* ON rules first, then OFF interlocks. This preserves legacy behavior for
       ordinary rules while enforcing the v3 invariant that de-energize wins. */
    for (int i = 0; i < SVC_RULE_MAX; ++i) {
        if (snap->rule[i].action != SVC_RULE_ACTION_OFF) {
            apply_rule(&snap->rule[i]);
        }
    }
    for (int i = 0; i < SVC_RULE_MAX; ++i) {
        if (snap->rule[i].action == SVC_RULE_ACTION_OFF) {
            apply_rule(&snap->rule[i]);
        }
    }
}
#endif /* CONFIG_SVC_USE_LOGIC_ENGINE */

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

static void ensure_relays_safe(void)
{
    if (relay_state_mask() != 0) {
        relay_apply_safe();
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
#if CONFIG_SVC_USE_LOGIC_ENGINE
                s_ruleset_dirty = true;    /* rebuild engine ruleset next eval */
#endif
                ESP_LOGI(TAG, "config reloaded");
            }
        }

#if !CONFIG_SVC_USE_LOGIC_ENGINE
        service_off_delays();
#endif

        if (rc == SVC_OK) {
            switch (ev.type) {
            case EVT_PRESENCE_CHANGED:
                s_presence_ok = true;
                evaluate_all_rules(&snap);
                break;
            case EVT_PRESENCE_STALE:
                s_presence_ok = false;
#if CONFIG_SVC_USE_LOGIC_ENGINE
                if (snap.fallback_din_enabled) {
                    /* SVC-012: a stale RS485 sensor falls back to the dry-contact
                       input instead of a blanket failsafe. */
                    ESP_LOGW(TAG, "presence stale -> dry-contact fallback");
                    evaluate_all_rules(&snap);
                } else {
                    ESP_LOGW(TAG, "presence stale, no fallback -> failsafe");
                    relay_apply_safe();
                }
#else
                /* Critical source lost: failsafe + fault indication. */
                ESP_LOGW(TAG, "presence stale -> failsafe");
                relay_apply_safe();
#endif
                break;
            case EVT_DINPUT_EDGE:
#if CONFIG_SVC_USE_LOGIC_ENGINE
                if (s_presence_ok || snap.fallback_din_enabled) {
                    evaluate_all_rules(&snap);
                } else {
                    ensure_relays_safe();
                }
#else
                if (s_presence_ok) {
                    evaluate_all_rules(&snap);
                } else {
                    ensure_relays_safe();
                }
#endif
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
#if CONFIG_SVC_USE_LOGIC_ENGINE
            if (s_presence_ok || snap.fallback_din_enabled) {
                evaluate_all_rules(&snap);
            } else {
                ensure_relays_safe();
            }
#else
            if (s_presence_ok) {
                evaluate_all_rules(&snap);
            } else {
                ensure_relays_safe();
            }
#endif
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
#if !CONFIG_SVC_USE_LOGIC_ENGINE
    memset((void *)s_off_deadline, 0, sizeof(s_off_deadline));
#endif

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
