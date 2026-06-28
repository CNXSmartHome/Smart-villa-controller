/**
 * @file eventbus.c
 * @brief Event queue implementation (see eventbus.h).
 */
#include "eventbus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdatomic.h>

static const char *TAG = "eventbus";

static QueueHandle_t s_queue;   /* created once in eventbus_init() */
static atomic_uint   s_dropped; /* events dropped due to a full queue */

svc_err_t eventbus_init(size_t depth)
{
    SVC_CHECK_ARG(depth > 0);
    if (s_queue != NULL) {
        return SVC_OK;   /* idempotent */
    }
    s_queue = xQueueCreate(depth, sizeof(svc_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "queue alloc failed (depth=%u)", (unsigned)depth);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "event bus ready (depth=%u, msg=%u bytes)",
             (unsigned)depth, (unsigned)sizeof(svc_event_t));
    return SVC_OK;
}

svc_err_t eventbus_post(const svc_event_t *ev, uint32_t timeout_ms)
{
    SVC_CHECK_ARG(ev != NULL);
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    svc_event_t copy = *ev;
    if (copy.t_ms == 0) {
        copy.t_ms = svc_now_ms();
    }
    if (xQueueSend(s_queue, &copy, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1);
        ESP_LOGW(TAG, "queue full, dropped event type=%d (total=%u)",
                 (int)copy.type, (unsigned)atomic_load(&s_dropped));
        return ESP_ERR_TIMEOUT;
    }
    return SVC_OK;
}

svc_err_t eventbus_post_isr(const svc_event_t *ev, bool *hp_task_woken)
{
    if (ev == NULL || s_queue == NULL) {
        return ESP_FAIL;
    }
    BaseType_t woken = pdFALSE;
    BaseType_t ok = xQueueSendFromISR(s_queue, ev, &woken);
    if (hp_task_woken != NULL) {
        *hp_task_woken = (woken == pdTRUE);
    }
    if (ok != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1);   /* no logging in ISR context */
        return ESP_FAIL;
    }
    return SVC_OK;
}

svc_err_t eventbus_receive(svc_event_t *out, uint32_t timeout_ms)
{
    SVC_CHECK_ARG(out != NULL);
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_queue, out, ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return SVC_OK;
}

uint32_t eventbus_dropped_count(void)
{
    return (uint32_t)atomic_load(&s_dropped);
}
