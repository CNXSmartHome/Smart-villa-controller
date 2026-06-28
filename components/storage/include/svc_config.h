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

#define SVC_CONFIG_VERSION   1
#define SVC_WIFI_SSID_MAX    32
#define SVC_WIFI_PASS_MAX    64
#define SVC_DEVICE_NAME_MAX  32

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

    uint32_t crc;   /**< CRC32 over all preceding bytes (integrity guard).      */
} svc_config_t;

/** @brief Populate @p cfg with factory defaults. */
void svc_config_defaults(svc_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SVC_CONFIG_H */
