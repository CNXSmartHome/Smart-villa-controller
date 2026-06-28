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

    /* 4. Relay polarity + safe state must be strict booleans. */
    for (int i = 0; i < BOARD_RELAY_COUNT; ++i) {
        cfg->relay_active_high[i] = norm_bool(cfg->relay_active_high[i]);
        cfg->relay_safe_on[i]     = norm_bool(cfg->relay_safe_on[i]);
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
        if (r->trigger_src == 1 && r->trigger_chan >= BOARD_DINPUT_COUNT) {
            r->enabled = 0;
        }
        if (r->target_relay >= BOARD_RELAY_COUNT) {
            r->enabled = 0;
        }
        r->off_delay_s = clamp_u16(r->off_delay_s, 0, SVC_OFF_DELAY_MAX_S > 0xFFFF
                                                       ? 0xFFFF : SVC_OFF_DELAY_MAX_S);
    }

    /* Keep the version stamp current. */
    cfg->version = SVC_CONFIG_VERSION;
}
