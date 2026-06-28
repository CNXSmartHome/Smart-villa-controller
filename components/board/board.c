/**
 * @file board.c
 * @brief SVC-100 V1 board bring-up (see board.h).
 */
#include "board.h"

static const char *TAG = "board";

static const gpio_num_t s_relay_gpio[BOARD_RELAY_COUNT] = {
    BOARD_GPIO_RELAY_0, BOARD_GPIO_RELAY_1,
    BOARD_GPIO_RELAY_2, BOARD_GPIO_RELAY_3,
};

static const gpio_num_t s_dinput_gpio[BOARD_DINPUT_COUNT] = {
    BOARD_GPIO_DIN_0, BOARD_GPIO_DIN_1, BOARD_GPIO_DIN_2, BOARD_GPIO_DIN_3,
    BOARD_GPIO_DIN_4, BOARD_GPIO_DIN_5, BOARD_GPIO_DIN_6, BOARD_GPIO_DIN_7,
};

static i2c_master_bus_handle_t s_i2c_bus;   /* NULL until board_init() */
static bool s_initialized;

static svc_err_t board_init_relays(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_RELAY_COUNT; ++i) {
        mask |= (1ULL << s_relay_gpio[i]);
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    SVC_RETURN_ON_ERR(gpio_config(&cfg));
    /* Drive de-energized (logic 0). The relay module re-applies polarity-aware
       safe state once it owns the channels; this guarantees no glitch at boot. */
    for (int i = 0; i < BOARD_RELAY_COUNT; ++i) {
        SVC_RETURN_ON_ERR(gpio_set_level(s_relay_gpio[i], 0));
    }
    return SVC_OK;
}

static svc_err_t board_init_inputs(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_DINPUT_COUNT; ++i) {
        mask |= (1ULL << s_dinput_gpio[i]);
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,   /* dinput module installs handlers */
    };
    SVC_RETURN_ON_ERR(gpio_config(&cfg));
    return SVC_OK;
}

static svc_err_t board_init_i2c(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port          = BOARD_I2C_PORT,
        .sda_io_num        = BOARD_GPIO_I2C_SDA,
        .scl_io_num        = BOARD_GPIO_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    SVC_RETURN_ON_ERR(i2c_new_master_bus(&cfg, &s_i2c_bus));
    return SVC_OK;
}

svc_err_t board_init(void)
{
    if (s_initialized) {
        return SVC_OK;
    }
    ESP_LOGI(TAG, "init V1 board: %d relays, %d inputs, %d RS485 buses",
             BOARD_RELAY_COUNT, BOARD_DINPUT_COUNT, BOARD_RS485_COUNT);

    SVC_RETURN_ON_ERR(board_init_relays());
    SVC_RETURN_ON_ERR(board_init_inputs());
    SVC_RETURN_ON_ERR(board_init_i2c());

    s_initialized = true;
    return SVC_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c_bus;
}

gpio_num_t board_relay_gpio(uint8_t index)
{
    return (index < BOARD_RELAY_COUNT) ? s_relay_gpio[index] : GPIO_NUM_NC;
}

gpio_num_t board_dinput_gpio(uint8_t index)
{
    return (index < BOARD_DINPUT_COUNT) ? s_dinput_gpio[index] : GPIO_NUM_NC;
}

bool board_config_button_held(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_BTN_CONFIG),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        return false;   /* fail closed: no button => no provisioning shortcut */
    }
    /* Active-low: a held button reads 0. */
    bool held = (gpio_get_level(BOARD_GPIO_BTN_CONFIG) == 0);
    if (held) {
        ESP_LOGW(TAG, "config button held at boot");
    }
    return held;
}
