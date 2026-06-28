/**
 * @file indicator.c
 * @brief LED + buzzer service implementation (see indicator.h).
 *
 * The LED rendering uses a periodic, non-blocking state machine: the task wakes
 * on a fixed tick, advances an animation phase, and writes the LED. Beep
 * requests are queued and rendered by toggling the LEDC duty for the requested
 * duration across ticks (still no busy-wait).
 */
#include "indicator.h"
#include "board.h"
#include "led_strip.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "indicator";

#define IND_TICK_MS        50
#define BUZZER_LEDC_TIMER  LEDC_TIMER_0
#define BUZZER_LEDC_CH     LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE   LEDC_LOW_SPEED_MODE

typedef struct {
    uint16_t freq_hz;
    uint16_t duration_ms;
    uint8_t  count;
} beep_cmd_t;

static led_strip_handle_t         s_led;
static volatile indicator_pattern_t s_pattern = IND_BOOT;
static QueueHandle_t              s_beep_q;
static TaskHandle_t               s_task;
static bool                       s_started;

static svc_err_t led_init(void)
{
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = BOARD_GPIO_LED_STRIP,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    SVC_RETURN_ON_ERR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led));
    return led_strip_clear(s_led);
}

static svc_err_t buzzer_init(void)
{
    const ledc_timer_config_t tcfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .timer_num = BUZZER_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    SVC_RETURN_ON_ERR(ledc_timer_config(&tcfg));
    const ledc_channel_config_t ccfg = {
        .gpio_num = BOARD_GPIO_BUZZER,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CH,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&ccfg);
}

static void buzzer_on(uint16_t freq_hz)
{
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, 512); /* 50% of 10-bit */
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
}

static void buzzer_off(void)
{
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
}

/** Compute RGB for a pattern at animation phase (0..19). */
static void render_led(indicator_pattern_t p, uint8_t phase)
{
    uint8_t r = 0, g = 0, b = 0;
    bool blink_on = (phase % 10) < 5;
    switch (p) {
    case IND_OFF:    break;
    case IND_BOOT:   r = 60; g = 30; break;                 /* amber       */
    case IND_OK:     g = (uint8_t)(8 + (phase % 10) * 5); break; /* breathe */
    case IND_CONFIG: if (blink_on) b = 80; break;           /* blue blink  */
    case IND_OTA:    r = (uint8_t)(phase * 4); b = (uint8_t)(phase * 4); break;
    case IND_FAULT:  if (blink_on) r = 100; break;          /* red blink   */
    }
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
}

static void indicator_task(void *arg)
{
    (void)arg;
    const TickType_t tick = pdMS_TO_TICKS(IND_TICK_MS);
    uint8_t phase = 0;
    beep_cmd_t beep = {0};
    uint8_t beeps_left = 0;
    int beep_ticks = 0;       /* >0: tone on; <0: gap                 */
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        /* Non-blocking: pick up a new beep request if idle. */
        if (beeps_left == 0 && beep_ticks == 0 &&
            xQueueReceive(s_beep_q, &beep, 0) == pdTRUE) {
            beeps_left = beep.count;
        }
        if (beeps_left > 0 && beep_ticks == 0) {
            buzzer_on(beep.freq_hz);
            beep_ticks = (int)(beep.duration_ms / IND_TICK_MS + 1);
        } else if (beep_ticks > 0) {
            if (--beep_ticks == 0) {
                buzzer_off();
                beep_ticks = -2;          /* short inter-beep gap        */
            }
        } else if (beep_ticks < 0) {
            if (++beep_ticks == 0 && beeps_left > 0) {
                beeps_left--;
            }
        }

        render_led(s_pattern, phase);
        phase = (uint8_t)((phase + 1) % 20);
        vTaskDelayUntil(&wake, tick);
    }
}

svc_err_t indicator_start(void)
{
    if (s_started) {
        return SVC_OK;
    }
    SVC_RETURN_ON_ERR(led_init());
    SVC_RETURN_ON_ERR(buzzer_init());

    s_beep_q = xQueueCreate(4, sizeof(beep_cmd_t));
    if (s_beep_q == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(indicator_task, "indicator", 2560, NULL, 3,
                                &s_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started");
    return SVC_OK;
}

svc_err_t indicator_set(indicator_pattern_t pattern)
{
    s_pattern = pattern;
    return SVC_OK;
}

svc_err_t indicator_beep(uint16_t freq_hz, uint16_t duration_ms, uint8_t count)
{
    if (s_beep_q == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    beep_cmd_t cmd = { .freq_hz = freq_hz ? freq_hz : 2000,
                       .duration_ms = duration_ms ? duration_ms : 80,
                       .count = count ? count : 1 };
    return (xQueueSend(s_beep_q, &cmd, 0) == pdTRUE) ? SVC_OK : ESP_ERR_TIMEOUT;
}
