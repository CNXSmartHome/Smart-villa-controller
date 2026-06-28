/**
 * @file svc_config.h
 * @brief Versioned, packed device configuration schema persisted in NVS.
 *
 * The whole struct is stored as one NVS blob. A `version` field drives forward
 * migration. Keep this layout stable; append new fields and bump the version
 * rather than reordering existing ones.
 */
#ifndef SVC_CONFIG_H
#define SVC_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_CONFIG_VERSION   2
#define SVC_WIFI_SSID_MAX    32
#define SVC_WIFI_PASS_MAX    64
#define SVC_DEVICE_NAME_MAX  32
#define SVC_SETUP_PW_MAX     64

/** @brief A single presence/inputs -> relay rule (data-driven control). */
typedef struct {
    uint8_t  enabled;        /**< 0/1                                          */
    uint8_t  trigger_src;    /**< 0 = presence, 1 = digital input             */
    uint8_t  trigger_chan;   /**< input channel (when src == 1)               */
    uint8_t  target_relay;   /**< relay index to drive                        */
    uint8_t  on_active;      /**< 1 = active condition energizes relay        */
    uint16_t off_delay_s;    /**< linger time before de-energizing            */
} svc_rule_t;

#define SVC_RULE_MAX 8

/** @brief Complete persisted configuration. */
typedef struct {
    uint16_t version;                          /**< == SVC_CONFIG_VERSION       */
    char     device_name[SVC_DEVICE_NAME_MAX]; /**< Friendly name / mDNS host   */

    /* Networking (optional layer) */
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;

    /* Relays: polarity + safe state per channel */
    uint8_t  relay_active_high[BOARD_RELAY_COUNT];
    uint8_t  relay_safe_on[BOARD_RELAY_COUNT];

    /* Digital inputs: per-channel active-low mask + debounce */
    uint8_t  din_active_low;                   /**< bitmask                     */
    uint16_t din_debounce_ms;

    /* Presence sensor (RS485 Modbus) */
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;

    /* Control rules */
    svc_rule_t rule[SVC_RULE_MAX];

    /* ---- Security / provisioning (added in schema v2) ---- */
    uint8_t  provisioned;       /**< 0 until first-time setup completes.        */
    uint8_t  webui_require_auth;/**< 1 = API auth required (default).           */
    char     setup_password[SVC_SETUP_PW_MAX]; /**< admin/setup password.       */

    uint32_t crc;   /**< CRC32 over all preceding bytes (integrity guard).      */
} svc_config_t;

/**
 * @brief Legacy v1 layout, retained ONLY so storage can migrate an existing v1
 *        blob into the current schema by intentional field copy. Do not use for
 *        anything else; it must never change.
 */
typedef struct {
    uint16_t version;
    char     device_name[SVC_DEVICE_NAME_MAX];
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;
    uint8_t  relay_active_high[BOARD_RELAY_COUNT];
    uint8_t  relay_safe_on[BOARD_RELAY_COUNT];
    uint8_t  din_active_low;
    uint16_t din_debounce_ms;
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;
    svc_rule_t rule[SVC_RULE_MAX];
    uint32_t crc;
} svc_config_v1_t;

/* Safe timing bounds enforced by svc_config_sanitize(). */
#define SVC_DIN_DEBOUNCE_MIN_MS   1
#define SVC_DIN_DEBOUNCE_MAX_MS   2000
#define SVC_PRESENCE_POLL_MIN_MS  50
#define SVC_PRESENCE_POLL_MAX_MS  10000
#define SVC_OFF_DELAY_MAX_S       86400   /* 24 h */
#define SVC_MODBUS_SLAVE_MIN      1
#define SVC_MODBUS_SLAVE_MAX      247

/** @brief Populate @p cfg with factory defaults. */
void svc_config_defaults(svc_config_t *cfg);

/**
 * @brief Sanitize an in-RAM configuration to enforce all safety invariants.
 *
 * Called on EVERY loaded configuration (current version included) after the CRC
 * check, and after migration. It is defensive: a CRC-valid but semantically
 * unsafe blob (e.g. forged in NVS, or corrupted in a way that preserves the
 * CRC) is made safe rather than trusted. Specifically it:
 *   - forces every fixed-size string array to be NUL-terminated;
 *   - normalizes all boolean fields to 0/1;
 *   - forces web/API auth enabled once the device is provisioned;
 *   - clamps timing values (debounce, poll, off-delay) to safe min/max;
 *   - constrains the Modbus slave address to 1..247;
 *   - replaces out-of-range rule targets/sources with safe defaults.
 *
 * @param cfg Configuration to sanitize in place. Must be non-NULL.
 */
void svc_config_sanitize(svc_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SVC_CONFIG_H */
