/**
 * @file storage.c
 * @brief NVS configuration store implementation (see storage.h).
 */
#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_crc.h"
#include "sdkconfig.h"   /* CONFIG_SVC_BOARD_ID (board guard) */
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
/** Map one frozen legacy rule into a v3 rule (new fields take safe defaults). */
static void migrate_rule(const svc_rule_legacy_t *src, svc_rule_t *dst)
{
    dst->enabled      = src->enabled;
    dst->trigger_src  = src->trigger_src;
    dst->trigger_chan = src->trigger_chan;
    dst->target_relay = src->target_relay;
    dst->on_active    = src->on_active;
    dst->off_delay_s  = src->off_delay_s;
    dst->for_ms       = 0;                    /* legacy rules had no dwell    */
    dst->action       = SVC_RULE_ACTION_ON;   /* legacy rules only drove ON   */
    dst->_reserved    = 0;
}

/* Copy the FROZEN legacy relay arrays ([SVC_LEGACY_RELAY]) into the first slots
   of the v5 arrays ([HAL_MAX_RELAY]) — never over-read the smaller source. */
static void copy_legacy_relays(svc_config_t *out,
                               const uint8_t *active_high, const uint8_t *safe_on)
{
    memcpy(out->relay_active_high, active_high, SVC_LEGACY_RELAY);
    memcpy(out->relay_safe_on, safe_on, SVC_LEGACY_RELAY);
}

/* Map the legacy single-presence fields into the v5 sensor[0] (RS485). */
static void map_legacy_presence(svc_config_t *out, uint8_t slave, uint16_t reg,
                                uint16_t present_min, uint16_t poll_ms)
{
    out->presence_slave = slave;          /* kept for back-compat */
    out->presence_reg = reg;
    out->presence_present_min = present_min;
    out->presence_poll_ms = poll_ms;
    out->presence_sensor_count = 1;
    out->presence_sensor[0].type = 1;     /* RS485 */
    out->presence_sensor[0].rs485_port = 0;
    out->presence_sensor[0].modbus_addr = slave ? slave : 1;
    out->presence_sensor[0].reg = reg;
    out->presence_sensor[0].present_min = present_min;
}

static bool config_migrate(const void *raw, size_t raw_len, svc_config_t *out)
{
    uint16_t ver = 0;
    if (raw_len >= sizeof(uint16_t)) {
        memcpy(&ver, raw, sizeof(ver));
    }

    /* out already holds current (v5) defaults; copy compatible fields only. */
    if (ver == 1 && raw_len == sizeof(svc_config_v1_t)) {
        const svc_config_v1_t *v1 = (const svc_config_v1_t *)raw;
        memcpy(out->device_name, v1->device_name, sizeof(out->device_name));
        out->wifi_enabled = v1->wifi_enabled;
        memcpy(out->wifi_ssid, v1->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v1->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v1->eth_enabled;
        copy_legacy_relays(out, v1->relay_active_high, v1->relay_safe_on);
        out->din_active_low = v1->din_active_low;   /* uint8 -> uint32 widen */
        out->din_debounce_ms = v1->din_debounce_ms;
        map_legacy_presence(out, v1->presence_slave, v1->presence_reg,
                            v1->presence_present_min, v1->presence_poll_ms);
        for (int i = 0; i < SVC_RULE_MAX; ++i) {
            migrate_rule(&v1->rule[i], &out->rule[i]);
        }
        /* v1 had no security fields: a migrated device must be re-provisioned. */
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v1 -> v%u (re-provisioning required)",
                 SVC_CONFIG_VERSION);
        return true;
    }

    if (ver == 2 && raw_len == sizeof(svc_config_v2_t)) {
        const svc_config_v2_t *v2 = (const svc_config_v2_t *)raw;
        memcpy(out->device_name, v2->device_name, sizeof(out->device_name));
        out->wifi_enabled = v2->wifi_enabled;
        memcpy(out->wifi_ssid, v2->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v2->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v2->eth_enabled;
        copy_legacy_relays(out, v2->relay_active_high, v2->relay_safe_on);
        out->din_active_low = v2->din_active_low;
        out->din_debounce_ms = v2->din_debounce_ms;
        map_legacy_presence(out, v2->presence_slave, v2->presence_reg,
                            v2->presence_present_min, v2->presence_poll_ms);
        for (int i = 0; i < SVC_RULE_MAX; ++i) {
            migrate_rule(&v2->rule[i], &out->rule[i]);
        }
        out->provisioned = v2->provisioned;      /* PRESERVE provisioning */
        out->webui_require_auth = v2->webui_require_auth;
        memcpy(out->setup_password, v2->setup_password, sizeof(out->setup_password));
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v2 -> v%u", SVC_CONFIG_VERSION);
        return true;
    }

    if (ver == 3 && raw_len == sizeof(svc_config_v3_t)) {
        const svc_config_v3_t *v3 = (const svc_config_v3_t *)raw;
        memcpy(out->device_name, v3->device_name, sizeof(out->device_name));
        out->wifi_enabled = v3->wifi_enabled;
        memcpy(out->wifi_ssid, v3->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v3->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v3->eth_enabled;
        copy_legacy_relays(out, v3->relay_active_high, v3->relay_safe_on);
        out->din_active_low = v3->din_active_low;
        out->din_debounce_ms = v3->din_debounce_ms;
        map_legacy_presence(out, v3->presence_slave, v3->presence_reg,
                            v3->presence_present_min, v3->presence_poll_ms);
        memcpy(out->rule, v3->rule, sizeof(out->rule));   /* v3 rule == v5 rule */
        out->provisioned = v3->provisioned;
        out->webui_require_auth = v3->webui_require_auth;
        memcpy(out->setup_password, v3->setup_password, sizeof(out->setup_password));
        out->fallback_din_enabled = v3->fallback_din_enabled;
        out->fallback_din_chan = v3->fallback_din_chan;
        /* v3 had no board_id: leave empty; storage stamps the current target. */
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v3 -> v%u", SVC_CONFIG_VERSION);
        return true;
    }

    if (ver == 4 && raw_len == sizeof(svc_config_v4_t)) {
        const svc_config_v4_t *v4 = (const svc_config_v4_t *)raw;
        memcpy(out->board_id, v4->board_id, sizeof(out->board_id));
        memcpy(out->device_name, v4->device_name, sizeof(out->device_name));
        out->wifi_enabled = v4->wifi_enabled;
        memcpy(out->wifi_ssid, v4->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v4->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v4->eth_enabled;
        memcpy(out->relay_active_high, v4->relay_active_high,
               sizeof(out->relay_active_high));        /* v4 arrays already [32] */
        memcpy(out->relay_safe_on, v4->relay_safe_on, sizeof(out->relay_safe_on));
        out->din_active_low = v4->din_active_low;
        out->din_debounce_ms = v4->din_debounce_ms;
        map_legacy_presence(out, v4->presence_slave, v4->presence_reg,
                            v4->presence_present_min, v4->presence_poll_ms);
        memcpy(out->rule, v4->rule, sizeof(out->rule));
        out->provisioned = v4->provisioned;
        out->webui_require_auth = v4->webui_require_auth;
        memcpy(out->setup_password, v4->setup_password, sizeof(out->setup_password));
        out->fallback_din_enabled = v4->fallback_din_enabled;
        out->fallback_din_chan = v4->fallback_din_chan;
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v4 -> v%u", SVC_CONFIG_VERSION);
        return true;
    }

    if (ver == 5 && raw_len == sizeof(svc_config_v5_t)) {
        const svc_config_v5_t *v5 = (const svc_config_v5_t *)raw;
        /* v6 == v5 + MQTT block. Copy the shared prefix field-by-field (NOT a
           prefix memcpy: v5's tail padding before crc differs from v6's MQTT
           block). The MQTT fields stay at the v6 defaults already in *out
           (disabled, remote control off) — preserving provisioning + everything. */
        memcpy(out->board_id, v5->board_id, sizeof(out->board_id));
        memcpy(out->device_name, v5->device_name, sizeof(out->device_name));
        out->wifi_enabled = v5->wifi_enabled;
        memcpy(out->wifi_ssid, v5->wifi_ssid, sizeof(out->wifi_ssid));
        memcpy(out->wifi_pass, v5->wifi_pass, sizeof(out->wifi_pass));
        out->eth_enabled = v5->eth_enabled;
        memcpy(out->relay_active_high, v5->relay_active_high,
               sizeof(out->relay_active_high));
        memcpy(out->relay_safe_on, v5->relay_safe_on, sizeof(out->relay_safe_on));
        out->din_active_low = v5->din_active_low;
        out->din_debounce_ms = v5->din_debounce_ms;
        out->presence_slave = v5->presence_slave;
        out->presence_reg = v5->presence_reg;
        out->presence_present_min = v5->presence_present_min;
        out->presence_poll_ms = v5->presence_poll_ms;
        memcpy(out->rule, v5->rule, sizeof(out->rule));
        out->provisioned = v5->provisioned;
        out->webui_require_auth = v5->webui_require_auth;
        memcpy(out->setup_password, v5->setup_password, sizeof(out->setup_password));
        out->fallback_din_enabled = v5->fallback_din_enabled;
        out->fallback_din_chan = v5->fallback_din_chan;
        out->presence_sensor_count = v5->presence_sensor_count;
        memcpy(out->presence_sensor, v5->presence_sensor,
               sizeof(out->presence_sensor));
        out->room_empty_delay_sec = v5->room_empty_delay_sec;
        out->sensor_fault_policy = v5->sensor_fault_policy;
        out->version = SVC_CONFIG_VERSION;
        ESP_LOGW(TAG, "migrated config v5 -> v%u", SVC_CONFIG_VERSION);
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

/* Expected board_id for THIS firmware build (Kconfig; empty if unset). */
#ifdef CONFIG_SVC_BOARD_ID
#define STORAGE_EXPECTED_BOARD_ID CONFIG_SVC_BOARD_ID
#else
#define STORAGE_EXPECTED_BOARD_ID ""
#endif

/**
 * @brief SVC-026 OTA/config board guard. A config tagged for a DIFFERENT board is
 *        rejected and factory-defaulted (prevents cross-board config/firmware
 *        mixups). An unbranded config is stamped with this build's board_id.
 */
static void apply_board_guard(svc_config_t *out, bool *was_default)
{
    const char *expected = STORAGE_EXPECTED_BOARD_ID;
    if (expected[0] == '\0') {
        return;   /* no target configured: nothing to enforce */
    }
    if (!svc_config_board_matches(out, expected)) {
        ESP_LOGE(TAG, "config board_id '%s' != target '%s'; factory-defaulting",
                 out->board_id, expected);
        svc_config_defaults(out);
        svc_config_set_board_id(out, expected);
        if (was_default) *was_default = true;
    } else if (out->board_id[0] == '\0') {
        svc_config_set_board_id(out, expected);   /* stamp unbranded config */
    }
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
        apply_board_guard(out, was_default);    /* SVC-026: reject wrong board */
        ESP_LOGI(TAG, "config loaded+sanitized (name=%s, provisioned=%u)",
                 out->device_name, out->provisioned);
        return SVC_OK;
    }

    /* Older schema: start from defaults, copy compatible fields intentionally. */
    svc_config_defaults(out);
    if (config_migrate(raw, len, out)) {
        svc_config_sanitize(out);
        /* A successful migration PRESERVES the prior config (v2/v3/v4 keep
           provisioning + setup_password; v1 has none, so provisioned stays 0
           and the device requires provisioning anyway). It is therefore NOT a
           factory default: leave *was_default as-is (false unless the board
           guard below re-defaults due to a wrong-board config). */
        apply_board_guard(out, was_default);    /* SVC-026: reject wrong board */
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
