/**
 * @file webui_settings.c
 * @brief Pure settings whitelist + apply (see webui_settings.h).
 */
#include "webui_settings.h"
#include <string.h>
#include <stdlib.h>

/* Installer-settable keys. Anything not here is rejected (fail closed), which
   deliberately EXCLUDES board_id/version/crc/provisioned/setup_password/
   webui_require_auth and relay polarity/safe-state. */
static const char *const WRITABLE_KEYS[] = {
    "device_name",
    "wifi_enabled", "wifi_ssid", "wifi_pass", "eth_enabled",
    "din_active_low", "din_debounce_ms",
    "room_empty_delay_sec", "sensor_fault_policy",
    "presence_sensor_count",
    "presence_1_type", "presence_1_rs485_port", "presence_1_modbus_addr",
    "presence_1_din_index", "presence_1_reg", "presence_1_present_min",
    "presence_2_type", "presence_2_rs485_port", "presence_2_modbus_addr",
    "presence_2_din_index", "presence_2_reg", "presence_2_present_min",
    "fallback_din_enabled", "fallback_din_chan",
    /* MQTT / CNX installer fields (schema v6, SVC-015 Phase 0). mqtt_pass is
       writable here (body only) but write-only: never returned by GET. This list
       still EXCLUDES setup_password/provisioned/webui_require_auth/board identity/
       relay polarity+safe-state. Remote control stays OFF unless explicitly set. */
    "mqtt_enabled", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
    "mqtt_client_id", "mqtt_topic_prefix", "mqtt_tls",
    "mqtt_allow_remote_control", "mqtt_command_timeout_ms",
};

/* Secret fields: writable via POST (body only) but NEVER returned by GET. */
static const char *const SECRET_KEYS[] = {
    "wifi_pass", "setup_password", "mqtt_pass",
};

bool webui_settings_is_writable(const char *key)
{
    if (key == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(WRITABLE_KEYS)/sizeof(WRITABLE_KEYS[0]); ++i) {
        if (strcmp(key, WRITABLE_KEYS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool webui_settings_is_secret(const char *key)
{
    if (key == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(SECRET_KEYS)/sizeof(SECRET_KEYS[0]); ++i) {
        if (strcmp(key, SECRET_KEYS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Bounded string copy into a fixed field. */
static void set_str(char *dst, size_t cap, const char *v)
{
    strncpy(dst, v, cap - 1);
    dst[cap - 1] = '\0';
}

static uint32_t to_u32(const char *v) { return (uint32_t)strtoul(v, NULL, 10); }

/* Apply a per-sensor field for sensor index @p si (0/1). */
static svc_err_t apply_sensor(svc_config_t *c, int si, const char *field,
                              const char *v)
{
    presence_sensor_cfg_t *s = &c->presence_sensor[si];
    if (strcmp(field, "type") == 0)        s->type = (uint8_t)to_u32(v);
    else if (strcmp(field, "rs485_port") == 0)  s->rs485_port = (uint8_t)to_u32(v);
    else if (strcmp(field, "modbus_addr") == 0) s->modbus_addr = (uint8_t)to_u32(v);
    else if (strcmp(field, "din_index") == 0)   s->din_index = (uint8_t)to_u32(v);
    else if (strcmp(field, "reg") == 0)         s->reg = (uint16_t)to_u32(v);
    else if (strcmp(field, "present_min") == 0) s->present_min = (uint16_t)to_u32(v);
    else return SVC_ERR_OUT_OF_RANGE;
    return SVC_OK;
}

svc_err_t webui_settings_apply(svc_config_t *cfg, const char *key,
                               const char *value)
{
    if (cfg == NULL || key == NULL || value == NULL) {
        return SVC_ERR_OUT_OF_RANGE;
    }
    if (!webui_settings_is_writable(key)) {
        return SVC_ERR_OUT_OF_RANGE;   /* fail closed: unknown/protected field */
    }

    if (strcmp(key, "device_name") == 0) {
        set_str(cfg->device_name, sizeof(cfg->device_name), value);
    } else if (strcmp(key, "wifi_enabled") == 0) {
        cfg->wifi_enabled = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "wifi_ssid") == 0) {
        set_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), value);
    } else if (strcmp(key, "wifi_pass") == 0) {
        set_str(cfg->wifi_pass, sizeof(cfg->wifi_pass), value);  /* body only */
    } else if (strcmp(key, "eth_enabled") == 0) {
        cfg->eth_enabled = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "din_active_low") == 0) {
        cfg->din_active_low = to_u32(value);
    } else if (strcmp(key, "din_debounce_ms") == 0) {
        cfg->din_debounce_ms = (uint16_t)to_u32(value);
    } else if (strcmp(key, "room_empty_delay_sec") == 0) {
        cfg->room_empty_delay_sec = (uint16_t)to_u32(value);
    } else if (strcmp(key, "sensor_fault_policy") == 0) {
        cfg->sensor_fault_policy = (uint8_t)to_u32(value);
    } else if (strcmp(key, "presence_sensor_count") == 0) {
        cfg->presence_sensor_count = (uint8_t)to_u32(value);
    } else if (strcmp(key, "fallback_din_enabled") == 0) {
        cfg->fallback_din_enabled = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "fallback_din_chan") == 0) {
        cfg->fallback_din_chan = (uint8_t)to_u32(value);
    } else if (strcmp(key, "mqtt_enabled") == 0) {
        cfg->mqtt_enabled = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "mqtt_host") == 0) {
        set_str(cfg->mqtt_host, sizeof(cfg->mqtt_host), value);
    } else if (strcmp(key, "mqtt_port") == 0) {
        /* Parse in u32 first: a raw >65535 (e.g. 70000) must NOT wrap into a
           valid-looking uint16 (4464). Out-of-range -> 0 so svc_config_sanitize
           defaults it to SVC_MQTT_PORT_DEFAULT (1883). */
        uint32_t p = to_u32(value);
        cfg->mqtt_port = (p == 0 || p > 65535) ? 0 : (uint16_t)p;
    } else if (strcmp(key, "mqtt_user") == 0) {
        set_str(cfg->mqtt_user, sizeof(cfg->mqtt_user), value);
    } else if (strcmp(key, "mqtt_pass") == 0) {
        set_str(cfg->mqtt_pass, sizeof(cfg->mqtt_pass), value);  /* body only, write-only */
    } else if (strcmp(key, "mqtt_client_id") == 0) {
        set_str(cfg->mqtt_client_id, sizeof(cfg->mqtt_client_id), value);
    } else if (strcmp(key, "mqtt_topic_prefix") == 0) {
        set_str(cfg->mqtt_topic_prefix, sizeof(cfg->mqtt_topic_prefix), value);
    } else if (strcmp(key, "mqtt_tls") == 0) {
        cfg->mqtt_tls = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "mqtt_allow_remote_control") == 0) {
        cfg->mqtt_allow_remote_control = to_u32(value) ? 1 : 0;
    } else if (strcmp(key, "mqtt_command_timeout_ms") == 0) {
        cfg->mqtt_command_timeout_ms = (uint16_t)to_u32(value);
    } else if (strncmp(key, "presence_1_", 11) == 0) {
        return apply_sensor(cfg, 0, key + 11, value);
    } else if (strncmp(key, "presence_2_", 11) == 0) {
        return apply_sensor(cfg, 1, key + 11, value);
    } else {
        return SVC_ERR_OUT_OF_RANGE;   /* unreachable: whitelist covers all */
    }
    return SVC_OK;
}
