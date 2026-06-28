/**
 * @file presence.c
 * @brief mmWave presence service implementation (see presence.h).
 */
#include "presence.h"
#include "eventbus.h"
#include "health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "presence";

/* Sane polling bounds: fast enough to feel responsive, slow enough not to flood
   the RS485 bus or starve other tasks. */
#define PRESENCE_POLL_MIN_MS 50
#define PRESENCE_POLL_MAX_MS 10000

static presence_cfg_t           s_cfg;
static volatile presence_state_t s_state = PRESENCE_UNKNOWN;
static TaskHandle_t             s_task;
static bool                     s_started;

static svc_err_t read_presence_raw(uint16_t *raw)
{
    if (s_cfg.use_input_reg) {
        return mb_read_input(s_cfg.master, s_cfg.slave_addr,
                             s_cfg.reg_presence, 1, raw);
    }
    return mb_read_holding(s_cfg.master, s_cfg.slave_addr,
                           s_cfg.reg_presence, 1, raw);
}

static void publish_state(presence_state_t st)
{
    if (st == s_state) {
        return;
    }
    s_state = st;
    svc_event_t ev = {
        .type = EVT_PRESENCE_CHANGED,
        .arg0 = (uint8_t)st,
        .arg1 = s_cfg.sensor_id,
    };
    (void)eventbus_post(&ev, 20);
    ESP_LOGI(TAG, "sensor %u -> %d", s_cfg.sensor_id, (int)st);
}

static void presence_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(s_cfg.poll_ms);
    uint32_t last_ok = svc_now_ms();
    bool stale_signaled = false;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        uint16_t raw = 0;
        svc_err_t rc = read_presence_raw(&raw);
        if (rc == SVC_OK) {
            last_ok = svc_now_ms();
            stale_signaled = false;
            publish_state(raw >= s_cfg.present_min ? PRESENCE_PRESENT
                                                   : PRESENCE_ABSENT);
            /* First successful poll => presence subsystem is healthy. */
            health_report(HEALTH_CHK_PRESENCE_RAN, true);
        } else if (!stale_signaled &&
                   (svc_now_ms() - last_ok) > s_cfg.stale_ms) {
            stale_signaled = true;
            s_state = PRESENCE_UNKNOWN;
            svc_event_t ev = { .type = EVT_PRESENCE_STALE,
                               .arg0 = s_cfg.sensor_id };
            (void)eventbus_post(&ev, 20);
            ESP_LOGW(TAG, "sensor %u stale (last read 0x%x)",
                     s_cfg.sensor_id, (int)rc);
            /* Degraded cleanly: the gate still counts this as "ran" so a missing
               sensor at the factory does not block a valid OTA, while the
               control engine independently applies failsafe. */
            health_report(HEALTH_CHK_PRESENCE_RAN, true);
        }
        /* Absolute-time delay keeps the cadence steady and watchdog-friendly. */
        vTaskDelayUntil(&wake, period);
    }
}

svc_err_t presence_start(const presence_cfg_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL && cfg->master != NULL);
    if (s_started) {
        return SVC_OK;
    }
    s_cfg = *cfg;
    /* Clamp poll period to a sane band and guarantee >= 1 scheduler tick so the
       task can never spin or be starved (P8). */
    uint32_t tick_ms = portTICK_PERIOD_MS ? portTICK_PERIOD_MS : 1;
    if (s_cfg.poll_ms < PRESENCE_POLL_MIN_MS) s_cfg.poll_ms = PRESENCE_POLL_MIN_MS;
    if (s_cfg.poll_ms > PRESENCE_POLL_MAX_MS) s_cfg.poll_ms = PRESENCE_POLL_MAX_MS;
    if (s_cfg.poll_ms < tick_ms)              s_cfg.poll_ms = (uint16_t)tick_ms;
    if (s_cfg.stale_ms == 0) s_cfg.stale_ms = 5000;
    if (s_cfg.stale_ms < s_cfg.poll_ms * 2)  s_cfg.stale_ms = s_cfg.poll_ms * 2;

    if (xTaskCreatePinnedToCore(presence_task, "presence", 3072, NULL, 4,
                                &s_task, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started (slave=%u, reg=%u, poll=%ums)",
             s_cfg.slave_addr, s_cfg.reg_presence, s_cfg.poll_ms);
    return SVC_OK;
}

presence_state_t presence_get(void)
{
    return s_state;
}
