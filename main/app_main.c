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
#include "hal_board.h"
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
#include "health.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

static const char *TAG = "app";

/* OTA validation gating (P3): mark the image valid only after health checks
   pass, never on a fixed timer. */
#define OTA_GATE_TIMEOUT_MS 30000  /* give up waiting for health after this */
#define WDT_STABLE_MS        8000  /* uptime with control alive => wdt stable */

/* Long-lived singletons that must outlive app_main; kept static (no heap). */
static svc_config_t  s_config;
static rs485_handle_t s_rs485a;
static mb_master_t    s_mb_a;
static bool          s_boot_button_held;   /* config button sampled at boot */

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
    /* SVC-025 phase 1: bring the board up THROUGH the HAL instead of calling
       board_init() directly. hal_install() registers the linked target's driver
       + profile (SVC-100 = native GPIO); hal_board_init() then runs that driver's
       init (== board_init()) and applies the de-energized safe state. Behavior is
       unchanged for SVC-100 — relay_init() below still sets polarity/safe state.
       The IO services (relay/dinput/rs485/…) are moved onto hal_* separately in
       SVC-024 phase 2; they still use BOARD_* directly here. */
    SVC_RETURN_ON_ERR(hal_install());
    SVC_RETURN_ON_ERR(hal_board_init());
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
    /* Relay safe state has been applied by relay_init() -> health check passes. */
    health_report(HEALTH_CHK_RELAY_SAFE, true);

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
    /* Tell the Web UI whether the operator is physically present (config button
       held at boot) so first-time provisioning can be gated to an explicit mode
       rather than ambient LAN. Must be set before webui_start(). */
    webui_set_provisioning_button(s_boot_button_held);

    /* Start the Web UI regardless; it binds the loopback/AP stack when present.
       On a standalone unit with no network it simply has no reachable clients. */
    rc = webui_start(&s_config);
    if (rc != SVC_OK) {
        ESP_LOGW(TAG, "webui_start: 0x%x (continuing standalone)", (int)rc);
    }
}

/**
 * @brief Validate the running OTA image ONLY after explicit health checks pass.
 *
 * Replaces the old fixed 5-second timer. We poll the health gate until either it
 * is satisfied (then mark the image valid) or a deadline elapses / a fault is
 * latched (then leave the image unconfirmed so the bootloader rolls back on the
 * next boot if this was a freshly-flashed OTA image).
 *
 * Mandatory checks (see health.h): control task alive, presence ran or degraded
 * cleanly, relay safe state applied, network stack settled, watchdog stable,
 * and no boot fault latched.
 */
static void validate_image_when_healthy(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    bool pending = (esp_ota_get_state_partition(running, &st) == ESP_OK &&
                    st == ESP_OTA_IMG_PENDING_VERIFY);

    uint32_t start = svc_now_ms();
    bool wdt_marked = false;

    while ((svc_now_ms() - start) < OTA_GATE_TIMEOUT_MS) {
        health_report_t hr;
        health_get(&hr);

        if (hr.fault_latched) {
            ESP_LOGE(TAG, "fault latched (%s); NOT validating image",
                     hr.fault_reason ? hr.fault_reason : "?");
            indicator_set(IND_FAULT);
            return;
        }
        /* Watchdog considered stable once the control task has been alive for a
           sustained window without a panic bringing us down. */
        if (!wdt_marked && hr.check[HEALTH_CHK_CONTROL_ALIVE] &&
            (svc_now_ms() - start) >= WDT_STABLE_MS) {
            health_report(HEALTH_CHK_WDT_STABLE, true);
            wdt_marked = true;
        }
        if (health_ota_gate_ok()) {
            if (pending) {
                ESP_LOGI(TAG, "all health checks passed; marking OTA image valid");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGI(TAG, "all health checks passed (not a pending OTA image)");
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (pending) {
        ESP_LOGE(TAG, "health gate not satisfied in %ums; leaving image "
                 "UNCONFIRMED (bootloader will roll back)", OTA_GATE_TIMEOUT_MS);
    } else {
        ESP_LOGW(TAG, "health gate not satisfied within boot window");
    }
}

void app_main(void)
{
    log_banner();
    health_init();

    /* Sample the config button as early as possible (before any driver
       reconfigures the pin) to decide the provisioning path. */
    s_boot_button_held = board_config_button_held();

    svc_err_t rc = bringup_core();
    if (rc != SVC_OK) {
        /* Core failed: latch a fault, signal it, and reboot rather than run
           half-initialized (the unconfirmed image will roll back). */
        health_latch_fault("core bring-up failed");
        ESP_LOGE(TAG, "core bring-up failed: %s", svc_err_to_name(rc));
        indicator_set(IND_FAULT);
        indicator_beep(800, 200, 5);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    bringup_optional();

    /* Validate the image strictly on health, not on a timer. */
    validate_image_when_healthy();

    ESP_LOGI(TAG, "boot complete; control engine running");
    /* app_main returns; all work continues in the spawned tasks. */
}
