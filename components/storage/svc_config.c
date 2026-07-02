/**
 * @file svc_config.c
 * @brief Configuration defaults and sanitization (see svc_config.h).
 *
 * This translation unit intentionally has NO ESP-IDF/NVS dependency so the
 * security-critical sanitizer can be unit-tested on the host. storage.c owns
 * the NVS/CRC/migration logic and calls into here.
 */
#include "svc_config.h"
#include "svc_common.h"
#include <string.h>

/** Force a fixed-size char array to be NUL-terminated (never read past it). */
#define NUL_TERMINATE(arr) do { (arr)[sizeof(arr) - 1] = '\0'; } while (0)

/** Normalize any non-zero value to exactly 1. */
static inline uint8_t norm_bool(uint8_t v) { return v ? 1u : 0u; }

static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void svc_config_defaults(svc_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = SVC_CONFIG_VERSION;
    strncpy(cfg->device_name, "SVC-100", sizeof(cfg->device_name) - 1);

    cfg->wifi_enabled = 0;     /* standalone by default */
    cfg->eth_enabled  = 0;

    for (int i = 0; i < HAL_MAX_RELAY; ++i) {
        cfg->relay_active_high[i] = 1;
        cfg->relay_safe_on[i]     = 0;   /* de-energized = safe */
    }

    cfg->din_active_low   = 0xFFFFFFFFu; /* opto/dry-contact inputs active-low */
    cfg->din_debounce_ms  = 30;
    cfg->board_id[0]      = '\0';        /* stamped by storage from build target */

    cfg->presence_slave        = 1;
    cfg->presence_reg          = 0x0000;
    cfg->presence_present_min  = 1;
    cfg->presence_poll_ms      = 500;

    /* v5 multi-sensor presence: one RS485 sensor by default, second disabled. */
    cfg->presence_sensor_count = 1;
    cfg->presence_sensor[0] = (presence_sensor_cfg_t){
        .type = 1 /*RS485*/, .rs485_port = 0, .modbus_addr = 1,
        .din_index = 0, .reg = 0x0000, .present_min = 1 };
    cfg->presence_sensor[1] = (presence_sensor_cfg_t){ .type = 0 /*disabled*/ };
    cfg->room_empty_delay_sec = 30;     /* don't drop AC/light for 30 s */
    cfg->sensor_fault_policy  = 0;      /* HOLD: a faulted sensor never falsely empties */

    /* Rule 0: presence -> relay 0, 30 s linger (a sensible villa default). */
    cfg->rule[0] = (svc_rule_t){ .enabled = 1, .trigger_src = 0,
                                 .trigger_chan = 0, .target_relay = 0,
                                 .on_active = 1, .off_delay_s = 30 };

    /* Security: ship UN-provisioned. Until the operator completes setup, the
       Web UI runs AP/local-only and ALL mutating routes are disabled. */
    cfg->provisioned        = 0;
    cfg->webui_require_auth = 1;
    cfg->setup_password[0]  = '\0';
}

void svc_config_sanitize(svc_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    svc_config_t def;
    svc_config_defaults(&def);

    /* 1. Every fixed-size string array must be NUL-terminated so later
          strnlen()/strlen() and JSON escaping can never read out of bounds. */
    NUL_TERMINATE(cfg->device_name);
    NUL_TERMINATE(cfg->wifi_ssid);
    NUL_TERMINATE(cfg->wifi_pass);
    NUL_TERMINATE(cfg->setup_password);
    NUL_TERMINATE(cfg->board_id);
    if (cfg->device_name[0] == '\0') {
        memcpy(cfg->device_name, def.device_name, sizeof(cfg->device_name));
    }

    /* 2. Normalize all booleans to 0/1. */
    cfg->wifi_enabled       = norm_bool(cfg->wifi_enabled);
    cfg->eth_enabled        = norm_bool(cfg->eth_enabled);
    cfg->provisioned        = norm_bool(cfg->provisioned);
    cfg->webui_require_auth = norm_bool(cfg->webui_require_auth);

    /* 3. Auth can never be turned off once provisioned (defence in depth; the
          mutating-route guard also ignores this flag). */
    if (cfg->provisioned && cfg->setup_password[0] != '\0') {
        cfg->webui_require_auth = 1;
    }
    /* A "provisioned" flag with an empty password is incoherent and unsafe:
       treat the device as un-provisioned so control stays locked. */
    if (cfg->provisioned && cfg->setup_password[0] == '\0') {
        cfg->provisioned = 0;
    }

    /* 4. Relay polarity must be strict boolean. Baseline failsafe always
          de-energizes relay outputs; persisted config must not make a safety
          path energize unknown loads. */
    for (int i = 0; i < HAL_MAX_RELAY; ++i) {
        cfg->relay_active_high[i] = norm_bool(cfg->relay_active_high[i]);
        cfg->relay_safe_on[i]     = 0;
    }

    /* 5. Clamp timing values to safe bands. */
    cfg->din_debounce_ms = clamp_u16(cfg->din_debounce_ms,
                                     SVC_DIN_DEBOUNCE_MIN_MS,
                                     SVC_DIN_DEBOUNCE_MAX_MS);
    cfg->presence_poll_ms = clamp_u16(cfg->presence_poll_ms,
                                      SVC_PRESENCE_POLL_MIN_MS,
                                      SVC_PRESENCE_POLL_MAX_MS);

    /* 6. Modbus slave address must be in the legal unicast range. */
    if (cfg->presence_slave < SVC_MODBUS_SLAVE_MIN ||
        cfg->presence_slave > SVC_MODBUS_SLAVE_MAX) {
        cfg->presence_slave = def.presence_slave;
    }

    /* 7. Rule fields: bound enums/indices and clamp the off delay. An out-of-
          range target/source disables the rule rather than driving a random
          relay. */
    for (int i = 0; i < SVC_RULE_MAX; ++i) {
        svc_rule_t *r = &cfg->rule[i];
        r->enabled   = norm_bool(r->enabled);
        r->on_active = norm_bool(r->on_active);
        if (r->trigger_src > 1) {
            r->trigger_src = 0;
            r->enabled = 0;
        }
        if (r->trigger_src == 1 && r->trigger_chan >= HAL_MAX_DIN) {
            r->enabled = 0;
        }
        if (r->target_relay >= HAL_MAX_RELAY) {
            r->enabled = 0;
        }
        r->off_delay_s = clamp_u16(r->off_delay_s, 0, SVC_OFF_DELAY_MAX_S > 0xFFFF
                                                       ? 0xFFFF : SVC_OFF_DELAY_MAX_S);
        /* v3 fields: action is ON unless explicitly OFF; dwell clamped. */
        r->action = (r->action == SVC_RULE_ACTION_OFF) ? SVC_RULE_ACTION_OFF
                                                       : SVC_RULE_ACTION_ON;
        r->for_ms = clamp_u16(r->for_ms, 0, SVC_RULE_FOR_MS_MAX);
        r->_reserved = 0;
    }

    /* 8. Dry-contact presence fallback (v3): normalize and ensure the chosen
          channel exists, else disable fallback rather than read a bad channel. */
    cfg->fallback_din_enabled = norm_bool(cfg->fallback_din_enabled);
    if (cfg->fallback_din_chan >= HAL_MAX_DIN) {
        cfg->fallback_din_chan = 0;
        cfg->fallback_din_enabled = 0;
    }

    /* 9. Multi-sensor presence (v5): bound counts, types, and per-sensor fields;
          an invalid sensor is DISABLED rather than read from a bad channel/addr. */
    if (cfg->presence_sensor_count > SVC_PRESENCE_MAX_SENSORS) {
        cfg->presence_sensor_count = SVC_PRESENCE_MAX_SENSORS;
    }
    for (int i = 0; i < SVC_PRESENCE_MAX_SENSORS; ++i) {
        presence_sensor_cfg_t *s = &cfg->presence_sensor[i];
        if (s->type > 2) {                 /* unknown type -> disabled */
            s->type = 0;
        }
        if (s->type == 1) {                /* RS485: valid port + slave addr */
            if (s->rs485_port >= HAL_MAX_RS485) s->type = 0;
            if (s->modbus_addr < SVC_MODBUS_SLAVE_MIN ||
                s->modbus_addr > SVC_MODBUS_SLAVE_MAX) s->type = 0;
        } else if (s->type == 2) {         /* dry contact: valid DI channel */
            if (s->din_index >= HAL_MAX_DIN) s->type = 0;
        }
    }
    cfg->room_empty_delay_sec = clamp_u16(cfg->room_empty_delay_sec, 0,
                                          SVC_ROOM_EMPTY_DELAY_MAX);
    if (cfg->sensor_fault_policy > 2) {
        cfg->sensor_fault_policy = 0;      /* default HOLD (never false-empties) */
    }

    /* Keep the version stamp current. */
    cfg->version = SVC_CONFIG_VERSION;
}

bool svc_config_board_matches(const svc_config_t *cfg, const char *expected_board_id)
{
    if (cfg == NULL || expected_board_id == NULL) {
        return false;
    }
    /* An empty/unbranded tag matches: the caller will stamp it. A populated tag
       that differs is a wrong-board config and must be rejected. */
    if (cfg->board_id[0] == '\0') {
        return true;
    }
    return strncmp(cfg->board_id, expected_board_id, sizeof(cfg->board_id)) == 0;
}

void svc_config_set_board_id(svc_config_t *cfg, const char *board_id)
{
    if (cfg == NULL || board_id == NULL) {
        return;
    }
    strncpy(cfg->board_id, board_id, sizeof(cfg->board_id) - 1);
    cfg->board_id[sizeof(cfg->board_id) - 1] = '\0';
}
