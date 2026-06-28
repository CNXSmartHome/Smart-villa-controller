/**
 * @file netmgr.h
 * @brief Wi-Fi / Ethernet connection manager (optional layer).
 *
 * Brings up networking only when enabled in config. Wi-Fi STA with AP fallback
 * for provisioning; optional Ethernet. Publishes EVT_NET_UP / EVT_NET_DOWN and
 * provides an SNTP time-sync hook. The core controller runs fully without this.
 *
 * @note V1 status: structural stub. The state machine and event wiring are in
 *       place; the concrete esp_wifi/esp_eth bring-up is marked TODO and returns
 *       SVC_ERR_NOT_IMPLEMENTED so the rest of the system compiles and runs.
 */
#ifndef NETMGR_H
#define NETMGR_H

#include "svc_common.h"
#include "svc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Link/interface state. */
typedef enum {
    NET_DOWN = 0,
    NET_CONNECTING,
    NET_UP,
    NET_AP_PROVISIONING,
} netmgr_state_t;

/**
 * @brief Start the connection manager.
 * @param cfg Device config (provides ssid/pass/enable flags). Must be non-NULL.
 * @return SVC_OK if started (or intentionally disabled), else an error.
 */
svc_err_t netmgr_start(const svc_config_t *cfg);

/** @brief Enter AP provisioning mode (e.g. on Config long-press). */
svc_err_t netmgr_enter_provisioning(void);

/** @brief Current network state snapshot. */
netmgr_state_t netmgr_state(void);

/** @brief True once an IP is acquired and the link is usable. */
bool netmgr_is_up(void);

#ifdef __cplusplus
}
#endif

#endif /* NETMGR_H */
