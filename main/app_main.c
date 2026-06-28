/**
 * @file app_main.c
 * @brief SVC-100 application entry point and boot orchestration.
 *
 * Brings the system up in dependency order (see docs/ARCHITECTURE.md §7):
 * platform -> config -> event bus -> drivers -> protocol/services -> control,
 * then the optional networking layer. Core control runs even if every optional
 * service is disabled or fails. After a successful boot window the running OTA
 * image is marked valid so the bootloader will not roll it back.
 */
#include "svc_common.h"
#include "svc_version.h"
#include "board.h"
#include "eventbus.h"
#include "storage.h"
#include "indicator.h"
#include "relay.h"
#include "dinput.h"
#include "button.h"
#include "rs485.h"
#include "modbus_master.h"
#include "presence.h"
#include "control.h"
#include "netmgr.h"
#include "webui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

static const char *TAG = "app";

/* Long-lived singletons that must outlive app_main; kept static (no heap). */
static svc_config_t  s_config;
static rs485_handle_t s_rs485a;
static mb_master_t    s_mb_a;

static void log_banner(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, " %s", SVC_PRODUCT_NAME);
    ESP_LOGI(TAG, " fw %s  hw %s  idf %s", SVC_FW_VERSION, SVC_HW_REVISION,
             app ? app->idf_ver : "?");
    ESP_LOGI(TAG, "=============================================");
}

/** Bring up the mandatory, standalone control path. Failure here is fatal. */
static svc_err_t bringup_core(void)
{
    SVC_RETURN_ON_ERR(board_init());
    SVC_RETURN_ON_ERR(eventbus_init(24));
    SVC_RETURN_ON_ERR(storage_init());

    bool defaulted = false;
    SVC_RETURN_ON_ERR(storage_load(&s_config, &defaulted));
    ESP_LOGI(TAG, "config %s", defaulted ? "(defaults)" : "(loaded)");

    SVC_RETURN_ON_ERR(indicator_start());
    indicator_set(IND_BOOT);

    /* Relays: apply polarity + safe state from config. */
    relay_cfg_t rcfg = {0};
    for (int i = 0; i < BOARD_RELAY_COUNT; ++i) {
        rcfg.channel[i].active_high = s_config.relay_active_high[i];
        rcfg.channel[i].safe_on     = s_config.relay_safe_on[i];
    }
    SVC_RETURN_ON_ERR(relay_init(&rcfg));

    /* Digital inputs. */
    dinput_cfg_t dcfg = { .debounce_ms = s_config.din_debounce_ms,
                          .active_low = s_config.din_active_low };
    SVC_RETURN_ON_ERR(dinput_start(&dcfg));

    /* Buttons. */
    SVC_RETURN_ON_ERR(button_start(NULL));

    /* RS485 bus A + Modbus master + presence sensor. */
    rs485_cfg_t bus = { .id = BOARD_RS485_A, .baud = 9600,
                        .data_bits = 8, .stop_bits = 1, .parity = 'N' };
    SVC_RETURN_ON_ERR(rs485_open(&bus, &s_rs485a));
    SVC_RETURN_ON_ERR(mb_master_init(&s_mb_a, s_rs485a, 200, 2));

    presence_cfg_t pcfg = {
        .master = &s_mb_a,
        .slave_addr = s_config.presence_slave,
        .reg_presence = s_config.presence_reg,
        .present_min = s_config.presence_present_min,
        .use_input_reg = 1,
        .poll_ms = s_config.presence_poll_ms,
        .stale_ms = 5000,
        .sensor_id = 0,
    };
    SVC_RETURN_ON_ERR(presence_start(&pcfg));

    /* The brain. */
    SVC_RETURN_ON_ERR(control_start(&s_config));
    return SVC_OK;
}

/** Bring up optional networking + Web UI. Failures degrade gracefully. */
static void bringup_optional(void)
{
    svc_err_t rc = netmgr_start(&s_config);
    if (rc != SVC_OK) {
        ESP_LOGW(TAG, "netmgr_start: 0x%x", (int)rc);
    }
    /* Start the Web UI regardless; it binds the loopback/AP stack when present.
       On a standalone unit with no network it simply has no reachable clients. */
    rc = webui_start(&s_config);
    if (rc != SVC_OK) {
        ESP_LOGW(TAG, "webui_start: 0x%x (continuing standalone)", (int)rc);
    }
}

/** Confirm the freshly-booted OTA image so the bootloader keeps it. */
static void confirm_ota_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "self-test passed, marking OTA image valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void app_main(void)
{
    log_banner();

    svc_err_t rc = bringup_core();
    if (rc != SVC_OK) {
        /* Core failed: signal fault and reboot rather than run half-initialized. */
        ESP_LOGE(TAG, "core bring-up failed: %s", svc_err_to_name(rc));
        indicator_set(IND_FAULT);
        indicator_beep(800, 200, 5);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    bringup_optional();

    /* Boot self-test window, then validate the running image for OTA rollback. */
    vTaskDelay(pdMS_TO_TICKS(5000));
    confirm_ota_image();

    ESP_LOGI(TAG, "boot complete; control engine running");
    /* app_main returns; all work continues in the spawned tasks. */
}
