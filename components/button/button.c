/**
 * @file button.c
 * @brief Config/Reset button handler implementation (see button.h).
 */
#include "button.h"
#include "board.h"
#include "eventbus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BTN_TICK_MS 10

typedef struct {
    gpio_num_t gpio;
    bool       pressed;        /* debounced state            */
    uint16_t   stable_ticks;   /* consecutive matching reads */
    uint32_t   press_ms;       /* time when press began      */
    bool       long_fired;     /* long event already emitted */
} btn_state_t;

static button_cfg_t s_cfg;
static btn_state_t  s_btn[BUTTON_COUNT];
static TaskHandle_t s_task;
static bool         s_started;

static void emit(button_id_t id, bool is_long)
{
    svc_event_t ev = { .type = is_long ? EVT_BUTTON_LONG : EVT_BUTTON_SHORT,
                       .arg0 = (uint8_t)id };
    (void)eventbus_post(&ev, 20);
    ESP_LOGI(TAG, "button %d %s press", id, is_long ? "long" : "short");
}

static void button_task(void *arg)
{
    (void)arg;
    const TickType_t tick = pdMS_TO_TICKS(BTN_TICK_MS);
    const uint16_t need = (uint16_t)(s_cfg.debounce_ms / BTN_TICK_MS + 1);
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        for (int i = 0; i < BUTTON_COUNT; ++i) {
            btn_state_t *b = &s_btn[i];
            bool raw_pressed = (gpio_get_level(b->gpio) == 0); /* active-low */

            if (raw_pressed == b->pressed) {
                b->stable_ticks = 0;
            } else if (++b->stable_ticks >= need) {
                b->stable_ticks = 0;
                b->pressed = raw_pressed;
                if (b->pressed) {
                    b->press_ms = svc_now_ms();
                    b->long_fired = false;
                } else if (!b->long_fired) {
                    emit((button_id_t)i, false); /* release before long => short */
                }
            }

            if (b->pressed && !b->long_fired &&
                (svc_now_ms() - b->press_ms) >= s_cfg.long_press_ms) {
                b->long_fired = true;
                emit((button_id_t)i, true);
            }
        }
        vTaskDelayUntil(&wake, tick);
    }
}

svc_err_t button_start(const button_cfg_t *cfg)
{
    if (s_started) {
        return SVC_OK;
    }
    s_cfg = cfg ? *cfg : (button_cfg_t){ .debounce_ms = 30, .long_press_ms = 3000 };
    if (s_cfg.debounce_ms == 0)   s_cfg.debounce_ms = 30;
    if (s_cfg.long_press_ms == 0) s_cfg.long_press_ms = 3000;

    s_btn[BUTTON_CONFIG].gpio = BOARD_GPIO_BTN_CONFIG;
    s_btn[BUTTON_RESET].gpio  = BOARD_GPIO_BTN_RESET;

    const gpio_config_t gc = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_BTN_CONFIG) |
                        (1ULL << BOARD_GPIO_BTN_RESET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,   /* polled, not interrupt-driven */
    };
    SVC_RETURN_ON_ERR(gpio_config(&gc));

    if (xTaskCreatePinnedToCore(button_task, "button", 2560, NULL, 4,
                                &s_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started (debounce=%ums, long=%ums)",
             s_cfg.debounce_ms, s_cfg.long_press_ms);
    return SVC_OK;
}
