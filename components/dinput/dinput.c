/**
 * @file dinput.c
 * @brief Debounced digital input driver implementation (see dinput.h).
 *
 * Design: a 1 kHz-ish periodic task samples all channels and runs the pure
 * integrator. The GPIO ISR only sends a task notification to wake sampling
 * promptly on the first edge; steady state still relies on the periodic tick so
 * a missed interrupt cannot wedge the input state. No blocking, no allocation
 * after start.
 */
#include "dinput.h"
#include "dinput_debounce.h"
#include "eventbus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dinput";

#define DINPUT_TICK_MS   2   /* sampling period */

static dinput_cfg_t       s_cfg;
static dinput_debounce_t  s_db[BOARD_DINPUT_COUNT];
static volatile uint8_t   s_stable_mask;
static TaskHandle_t       s_task;
static bool               s_started;

/** Raw read translated to logical-active using the active-low mask. */
static inline bool read_active(uint8_t ch)
{
    int raw = gpio_get_level(board_dinput_gpio(ch));
    bool active_low = (s_cfg.active_low >> ch) & 0x1;
    return active_low ? (raw == 0) : (raw != 0);
}

static void IRAM_ATTR dinput_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    if (s_task != NULL) {
        vTaskNotifyGiveFromISR(s_task, &hp);
    }
    if (hp == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void dinput_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(DINPUT_TICK_MS);
    for (;;) {
        /* Wake on edge notification or fall back to the periodic tick. */
        ulTaskNotifyTake(pdTRUE, period);

        for (uint8_t ch = 0; ch < BOARD_DINPUT_COUNT; ++ch) {
            bool raw = read_active(ch);
            if (dinput_debounce_update(&s_db[ch], raw)) {
                bool level = s_db[ch].stable;
                if (level) {
                    s_stable_mask |= (uint8_t)(1U << ch);
                } else {
                    s_stable_mask &= (uint8_t)~(1U << ch);
                }
                svc_event_t ev = {
                    .type = EVT_DINPUT_EDGE,
                    .arg0 = ch,
                    .arg1 = level ? 1 : 0,
                };
                (void)eventbus_post(&ev, 10);
                ESP_LOGD(TAG, "ch%u edge -> %d", ch, level);
            }
        }
    }
}

svc_err_t dinput_start(const dinput_cfg_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (s_started) {
        return SVC_OK;
    }
    s_cfg = *cfg;

    uint8_t ticks = (uint8_t)svc_clamp_i32(cfg->debounce_ms / DINPUT_TICK_MS, 1, 255);
    for (uint8_t ch = 0; ch < BOARD_DINPUT_COUNT; ++ch) {
        bool active = read_active(ch);
        dinput_debounce_init(&s_db[ch], ticks, active);
        if (active) {
            s_stable_mask |= (uint8_t)(1U << ch);
        }
    }

    /* The GPIO ISR service may already be installed by another driver. */
    esp_err_t isr_rc = gpio_install_isr_service(0);
    if (isr_rc != ESP_OK && isr_rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "isr service install failed: 0x%x", (int)isr_rc);
        return isr_rc;
    }
    /* Track how many handlers we add so we can unwind on any later failure. */
    uint8_t added = 0;
    for (uint8_t ch = 0; ch < BOARD_DINPUT_COUNT; ++ch) {
        esp_err_t rc = gpio_isr_handler_add(board_dinput_gpio(ch),
                                            dinput_isr, NULL);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "isr add ch%u failed: 0x%x; unwinding", ch, (int)rc);
            for (uint8_t j = 0; j < added; ++j) {
                gpio_isr_handler_remove(board_dinput_gpio(j));
            }
            return rc;
        }
        added++;
    }

    if (xTaskCreatePinnedToCore(dinput_task, "dinput", 3072, NULL, 5,
                                &s_task, 1) != pdPASS) {
        /* Task did not start: remove every handler we installed above. */
        for (uint8_t ch = 0; ch < BOARD_DINPUT_COUNT; ++ch) {
            gpio_isr_handler_remove(board_dinput_gpio(ch));
        }
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started (%d ch, debounce=%ums, active_low=0x%02x)",
             BOARD_DINPUT_COUNT, cfg->debounce_ms, cfg->active_low);
    return SVC_OK;
}

svc_err_t dinput_get(uint8_t channel, bool *out_active)
{
    SVC_CHECK_ARG(channel < BOARD_DINPUT_COUNT && out_active != NULL);
    *out_active = (s_stable_mask >> channel) & 0x1;
    return SVC_OK;
}

uint8_t dinput_state_mask(void)
{
    return s_stable_mask;
}
