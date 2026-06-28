/**
 * @file storage.c
 * @brief NVS configuration store implementation (see storage.h).
 */
#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_crc.h"
#include <string.h>

static const char *TAG = "storage";

#define NVS_NS   "svc"
#define NVS_KEY  "config"

void svc_config_defaults(svc_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = SVC_CONFIG_VERSION;
    strncpy(cfg->device_name, "SVC-100", sizeof(cfg->device_name) - 1);

    cfg->wifi_enabled = 0;     /* standalone by default */
    cfg->eth_enabled  = 0;

    for (int i = 0; i < BOARD_RELAY_COUNT; ++i) {
        cfg->relay_active_high[i] = 1;
        cfg->relay_safe_on[i]     = 0;   /* de-energized = safe */
    }

    cfg->din_active_low   = 0xFF;        /* opto inputs active-low */
    cfg->din_debounce_ms  = 30;

    cfg->presence_slave        = 1;
    cfg->presence_reg          = 0x0000;
    cfg->presence_present_min  = 1;
    cfg->presence_poll_ms      = 500;

    /* Rule 0: presence -> relay 0, 30 s linger (a sensible villa default). */
    cfg->rule[0] = (svc_rule_t){ .enabled = 1, .trigger_src = 0,
                                 .trigger_chan = 0, .target_relay = 0,
                                 .on_active = 1, .off_delay_s = 30 };
}

static uint32_t config_crc(const svc_config_t *cfg)
{
    /* CRC covers everything up to the trailing crc field. */
    size_t len = offsetof(svc_config_t, crc);
    return esp_crc32_le(0, (const uint8_t *)cfg, len);
}

svc_err_t storage_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs needs erase (0x%x), reformatting", (int)rc);
        SVC_RETURN_ON_ERR(nvs_flash_erase());
        rc = nvs_flash_init();
    }
    SVC_RETURN_ON_ERR(rc);
    ESP_LOGI(TAG, "nvs ready");
    return SVC_OK;
}

svc_err_t storage_load(svc_config_t *out, bool *was_default)
{
    SVC_CHECK_ARG(out != NULL);
    if (was_default) {
        *was_default = false;
    }

    nvs_handle_t h;
    esp_err_t rc = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (rc == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "no config namespace; using defaults");
        svc_config_defaults(out);
        if (was_default) *was_default = true;
        return SVC_OK;
    }
    SVC_RETURN_ON_ERR(rc);

    size_t len = sizeof(*out);
    rc = nvs_get_blob(h, NVS_KEY, out, &len);
    nvs_close(h);

    if (rc != ESP_OK || len != sizeof(*out)) {
        ESP_LOGW(TAG, "config missing/size-mismatch (0x%x); using defaults", (int)rc);
        svc_config_defaults(out);
        if (was_default) *was_default = true;
        return SVC_OK;
    }
    if (out->crc != config_crc(out)) {
        ESP_LOGE(TAG, "config CRC bad; using defaults");
        svc_config_defaults(out);
        if (was_default) *was_default = true;
        return SVC_OK;
    }
    if (out->version > SVC_CONFIG_VERSION) {
        ESP_LOGE(TAG, "config version %u > fw %u", out->version, SVC_CONFIG_VERSION);
        return SVC_ERR_CONFIG_VERSION;
    }
    if (out->version < SVC_CONFIG_VERSION) {
        /* Forward-migration hook: fields added later are zero-initialized in NVS.
           Bump version and let new defaults apply on next save. */
        ESP_LOGW(TAG, "migrating config v%u -> v%u", out->version, SVC_CONFIG_VERSION);
        out->version = SVC_CONFIG_VERSION;
    }
    ESP_LOGI(TAG, "config loaded (name=%s)", out->device_name);
    return SVC_OK;
}

svc_err_t storage_save(svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    cfg->version = SVC_CONFIG_VERSION;
    cfg->crc = config_crc(cfg);

    nvs_handle_t h;
    SVC_RETURN_ON_ERR(nvs_open(NVS_NS, NVS_READWRITE, &h));
    esp_err_t rc = nvs_set_blob(h, NVS_KEY, cfg, sizeof(*cfg));
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    SVC_RETURN_ON_ERR(rc);
    ESP_LOGI(TAG, "config saved");
    return SVC_OK;
}

svc_err_t storage_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t rc = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (rc == ESP_ERR_NVS_NOT_FOUND) {
        return SVC_OK;
    }
    SVC_RETURN_ON_ERR(rc);
    rc = nvs_erase_all(h);
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGW(TAG, "factory reset (0x%x)", (int)rc);
    return rc;
}
