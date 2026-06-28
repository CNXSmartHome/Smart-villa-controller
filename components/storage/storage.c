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

/* svc_config_defaults() and svc_config_sanitize() live in svc_config.c (no NVS
   dependency) so the sanitizer can be host-unit-tested. */

/**
 * @brief Migrate a stored blob of an older schema into the current layout.
 *
 * The pattern is deliberate: start from current defaults, then copy only the
 * fields that are known to be compatible from the old struct. This is safer
 * than a raw memcpy because it never carries over removed/reinterpreted bytes
 * and it guarantees new fields hold sane defaults.
 *
 * @param raw      Raw bytes read from NVS.
 * @param raw_len  Number of bytes read.
 * @param out      Destination (already defaulted by the caller).
 * @return true if a known old version was migrated, false otherwise.
 */
static bool config_migrate(const void *raw, size_t raw_len, svc_config_t *out)
{
    uint16_t ver = 0;
    if (raw_len >= sizeof(uint16_t)) {
        memcpy(&ver, raw, sizeof(ver));
    }
    if (ver == 1 && raw_len == sizeof(svc_config_v1_t)) {
        const svc_config_v1_t *v1 = (const svc_config_v1_t *)raw;
        /* out already holds v2 defaults; copy compatible fields intentionally. */
        memcpy(out->device_name, v1->device_name, sizeof(out->device_name));
        out->wifi_enabled = v1->wifi_enabled;
        memcpy(out->wifi_ssid, v1->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v1->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v1->eth_enabled;
        memcpy(out->relay_active_high, v1->relay_active_high,
               sizeof(out->relay_active_high));
        memcpy(out->relay_safe_on, v1->relay_safe_on, sizeof(out->relay_safe_on));
        out->din_active_low = v1->din_active_low;
        out->din_debounce_ms = v1->din_debounce_ms;
        out->presence_slave = v1->presence_slave;
        out->presence_reg = v1->presence_reg;
        out->presence_present_min = v1->presence_present_min;
        out->presence_poll_ms = v1->presence_poll_ms;
        memcpy(out->rule, v1->rule, sizeof(out->rule));
        /* New v2 security fields keep their (un-provisioned) defaults: a
           migrated device must be re-provisioned before relay control. */
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v1 -> v%u (re-provisioning required)",
                 SVC_CONFIG_VERSION);
        return true;
    }
    return false;
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

    /* Read into a raw buffer that can hold either the current or any known
       legacy layout, so migration can inspect the original bytes. */
    uint8_t raw[sizeof(svc_config_t)];
    size_t len = sizeof(raw);
    rc = nvs_get_blob(h, NVS_KEY, raw, &len);
    nvs_close(h);

    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "config read failed (0x%x); using defaults", (int)rc);
        svc_config_defaults(out);
        if (was_default) *was_default = true;
        return SVC_OK;
    }

    uint16_t stored_ver = 0;
    if (len >= sizeof(uint16_t)) {
        memcpy(&stored_ver, raw, sizeof(stored_ver));
    }
    if (stored_ver > SVC_CONFIG_VERSION) {
        ESP_LOGE(TAG, "config version %u > fw %u (refusing)",
                 stored_ver, SVC_CONFIG_VERSION);
        return SVC_ERR_CONFIG_VERSION;
    }

    /* Current schema: accept only if size and CRC both validate. */
    if (stored_ver == SVC_CONFIG_VERSION && len == sizeof(svc_config_t)) {
        memcpy(out, raw, sizeof(*out));
        if (out->crc != config_crc(out)) {
            ESP_LOGE(TAG, "config CRC bad; using defaults");
            svc_config_defaults(out);
            if (was_default) *was_default = true;
            return SVC_OK;
        }
        /* Defence in depth: a CRC-valid blob may still hold semantically unsafe
           values (forged/partially-corrupted). Sanitize before trusting it. */
        svc_config_sanitize(out);
        ESP_LOGI(TAG, "config loaded+sanitized (name=%s, provisioned=%u)",
                 out->device_name, out->provisioned);
        return SVC_OK;
    }

    /* Older schema: start from defaults, copy compatible fields intentionally. */
    svc_config_defaults(out);
    if (config_migrate(raw, len, out)) {
        svc_config_sanitize(out);
        if (was_default) *was_default = true;   /* security fields re-defaulted */
        return SVC_OK;
    }

    ESP_LOGW(TAG, "unrecognized config (ver=%u,len=%u); using defaults",
             stored_ver, (unsigned)len);
    if (was_default) *was_default = true;
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
