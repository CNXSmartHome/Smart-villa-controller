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
#include "hal_board.h"   /* HAL_MAX_RELAY/DIN, HAL_BOARD_ID_MAX — target-neutral */

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_CONFIG_VERSION   5
#define SVC_WIFI_SSID_MAX    32
#define SVC_WIFI_PASS_MAX    64
#define SVC_DEVICE_NAME_MAX  32
#define SVC_SETUP_PW_MAX     64
#define SVC_PRESENCE_MAX_SENSORS 2   /* up to 2 mmWave sensors per controller */

/** @brief One presence sensor channel (schema v5). type: 0=disabled,1=RS485,2=dry. */
typedef struct {
    uint8_t  type;        /**< presence_sensor_type_t (0/1/2).               */
    uint8_t  rs485_port;  /**< RS485 bus index (type==RS485).                */
    uint8_t  modbus_addr; /**< Modbus slave 1..247 (type==RS485).            */
    uint8_t  din_index;   /**< digital-input channel (type==DRY_CONTACT).    */
    uint16_t reg;         /**< RS485 presence register.                      */
    uint16_t present_min; /**< value >= present_min => PRESENT.              */
} presence_sensor_cfg_t;

#define SVC_RULE_MAX 8

/* Frozen channel counts for the SVC-100-era schemas v1..v3 (board.h removed).
   Never change — used only to size/migrate old NVS blobs. */
#define SVC_LEGACY_RELAY 4
#define SVC_LEGACY_DIN   8

/**
 * @brief FROZEN legacy rule layout used by schema v1 and v2. Never change this
 *        type — it exists only so storage can size and migrate old NVS blobs.
 */
typedef struct {
    uint8_t  enabled;
    uint8_t  trigger_src;    /**< 0 = presence, 1 = digital input             */
    uint8_t  trigger_chan;
    uint8_t  target_relay;
    uint8_t  on_active;      /**< 1 = active condition energizes relay        */
    uint16_t off_delay_s;
} svc_rule_legacy_t;

/** @brief Logic action for a rule (schema v3). */
typedef enum {
    SVC_RULE_ACTION_ON  = 0, /**< drive target relay ON while triggered.      */
    SVC_RULE_ACTION_OFF = 1, /**< force target relay OFF (safety interlock).  */
} svc_rule_action_t;

/**
 * @brief A single presence/inputs -> relay rule (schema v3).
 *
 * v3 adds @ref for_ms (dwell: the condition must hold continuously this long
 * before it counts — the logic engine's FOR) and @ref action (ON, or an OFF
 * interlock). The legacy @ref on_active is retained: it selects the trigger
 * polarity (active vs inactive) the logic adapter maps to the engine's cond.
 */
typedef struct {
    uint8_t  enabled;        /**< 0/1                                          */
    uint8_t  trigger_src;    /**< 0 = presence, 1 = digital input             */
    uint8_t  trigger_chan;   /**< input channel (when src == 1)               */
    uint8_t  target_relay;   /**< relay index to drive                        */
    uint8_t  on_active;      /**< 1 = active is the trigger polarity, 0 = inactive */
    uint16_t off_delay_s;    /**< linger time before de-energizing            */
    uint16_t for_ms;         /**< (v3) dwell: condition must hold this long    */
    uint8_t  action;         /**< (v3) svc_rule_action_t                       */
    uint8_t  _reserved;      /**< (v3) padding/future use; keep 0              */
} svc_rule_t;

/** @brief Complete persisted configuration (schema v4 — target-neutral). */
typedef struct {
    uint16_t version;                          /**< == SVC_CONFIG_VERSION       */
    char     board_id[HAL_BOARD_ID_MAX];       /**< (v4) board this config is for */
    char     device_name[SVC_DEVICE_NAME_MAX]; /**< Friendly name / mDNS host   */

    /* Networking (optional layer) */
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;

    /* Relays: polarity + safe state per channel (sized to HAL max across targets) */
    uint8_t  relay_active_high[HAL_MAX_RELAY];
    uint8_t  relay_safe_on[HAL_MAX_RELAY];

    /* Digital inputs: per-channel active-low mask (v4: 32-bit) + debounce */
    uint32_t din_active_low;                   /**< bitmask (<= HAL_MAX_DIN)     */
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

    /* ---- Dry-contact presence fallback (SVC-012, added in schema v3) ---- */
    uint8_t  fallback_din_enabled; /**< use a DI channel as presence when RS485 stale */
    uint8_t  fallback_din_chan;    /**< digital-input channel carrying mmWave S1/S2  */

    /* ---- Multi-sensor presence + fusion (schema v5) ---- */
    uint8_t  presence_sensor_count;                 /**< 0..SVC_PRESENCE_MAX_SENSORS */
    presence_sensor_cfg_t presence_sensor[SVC_PRESENCE_MAX_SENSORS];
    uint16_t room_empty_delay_sec;                  /**< both-empty hold before OFF */
    uint8_t  sensor_fault_policy;                   /**< sensor_fault_policy_t (0/1/2) */

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
    uint8_t  relay_active_high[SVC_LEGACY_RELAY];
    uint8_t  relay_safe_on[SVC_LEGACY_RELAY];
    uint8_t  din_active_low;
    uint16_t din_debounce_ms;
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;
    svc_rule_legacy_t rule[SVC_RULE_MAX];
    uint32_t crc;
} svc_config_v1_t;

/**
 * @brief FROZEN legacy v2 layout, retained ONLY for migration to v3. Mirrors the
 *        v2 schema exactly (legacy rule type + the v2 security fields). Never change.
 */
typedef struct {
    uint16_t version;
    char     device_name[SVC_DEVICE_NAME_MAX];
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;
    uint8_t  relay_active_high[SVC_LEGACY_RELAY];
    uint8_t  relay_safe_on[SVC_LEGACY_RELAY];
    uint8_t  din_active_low;
    uint16_t din_debounce_ms;
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;
    svc_rule_legacy_t rule[SVC_RULE_MAX];
    uint8_t  provisioned;
    uint8_t  webui_require_auth;
    char     setup_password[SVC_SETUP_PW_MAX];
    uint32_t crc;
} svc_config_v2_t;

/**
 * @brief FROZEN legacy v3 layout (SVC-100, 4 relays / 8 inputs, uint8 din mask,
 *        v3 rule type, no board_id). Retained ONLY for migration to v4.
 */
typedef struct {
    uint16_t version;
    char     device_name[SVC_DEVICE_NAME_MAX];
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;
    uint8_t  relay_active_high[SVC_LEGACY_RELAY];
    uint8_t  relay_safe_on[SVC_LEGACY_RELAY];
    uint8_t  din_active_low;
    uint16_t din_debounce_ms;
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;
    svc_rule_t rule[SVC_RULE_MAX];
    uint8_t  provisioned;
    uint8_t  webui_require_auth;
    char     setup_password[SVC_SETUP_PW_MAX];
    uint8_t  fallback_din_enabled;
    uint8_t  fallback_din_chan;
    uint32_t crc;
} svc_config_v3_t;

/**
 * @brief FROZEN legacy v4 layout (target-neutral: board_id, 32 relays, uint32 din
 *        mask) WITHOUT the v5 multi-sensor block. Retained ONLY for v4->v5 migration.
 */
typedef struct {
    uint16_t version;
    char     board_id[HAL_BOARD_ID_MAX];
    char     device_name[SVC_DEVICE_NAME_MAX];
    uint8_t  wifi_enabled;
    char     wifi_ssid[SVC_WIFI_SSID_MAX];
    char     wifi_pass[SVC_WIFI_PASS_MAX];
    uint8_t  eth_enabled;
    uint8_t  relay_active_high[HAL_MAX_RELAY];
    uint8_t  relay_safe_on[HAL_MAX_RELAY];
    uint32_t din_active_low;
    uint16_t din_debounce_ms;
    uint8_t  presence_slave;
    uint16_t presence_reg;
    uint16_t presence_present_min;
    uint16_t presence_poll_ms;
    svc_rule_t rule[SVC_RULE_MAX];
    uint8_t  provisioned;
    uint8_t  webui_require_auth;
    char     setup_password[SVC_SETUP_PW_MAX];
    uint8_t  fallback_din_enabled;
    uint8_t  fallback_din_chan;
    uint32_t crc;
} svc_config_v4_t;

/* Safe timing bounds enforced by svc_config_sanitize(). */
#define SVC_DIN_DEBOUNCE_MIN_MS   1
#define SVC_DIN_DEBOUNCE_MAX_MS   2000
#define SVC_PRESENCE_POLL_MIN_MS  50
#define SVC_PRESENCE_POLL_MAX_MS  10000
#define SVC_OFF_DELAY_MAX_S       86400   /* 24 h */
#define SVC_MODBUS_SLAVE_MIN      1
#define SVC_MODBUS_SLAVE_MAX      247
#define SVC_RULE_FOR_MS_MAX       60000   /* 60 s max dwell */
#define SVC_ROOM_EMPTY_DELAY_MAX  3600    /* 1 h max empty-hold */

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

/**
 * @brief True if @p cfg is tagged for @p expected_board_id (empty tag matches —
 *        treated as "unbranded", caller will stamp it).
 */
bool svc_config_board_matches(const svc_config_t *cfg, const char *expected_board_id);

/** @brief Stamp @p cfg with @p board_id (NUL-terminated, bounded copy). */
void svc_config_set_board_id(svc_config_t *cfg, const char *board_id);

#ifdef __cplusplus
}
#endif

#endif /* SVC_CONFIG_H */
