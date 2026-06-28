/**
 * @file webui_authz.h
 * @brief Pure authorization decision functions for the Web UI.
 *
 * These are the ACTUAL gate predicates the HTTP handlers in webui.c call — not
 * a documentation mirror. They are deliberately free of any esp_http_server
 * dependency so they can be unit-tested directly on the host, which means a
 * test failure here reflects a real change in production behavior.
 */
#ifndef WEBUI_AUTHZ_H
#define WEBUI_AUTHZ_H

#include <stdbool.h>
#include "netmgr.h"   /* netmgr_state_t, NET_AP_PROVISIONING */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether first-time provisioning is permitted right now.
 *
 * Never once provisioned. Otherwise requires an EXPLICIT provisioning mode:
 * the physical config button held at boot, or active AP provisioning mode.
 * There is intentionally NO ambient-LAN time window — a freshly reset unit on
 * a normal network must not be silently provisionable by a remote client.
 *
 * @param provisioned  Device already has a setup password.
 * @param boot_button  Config button was held during boot.
 * @param net_state    Current network manager state.
 */
bool webui_provisioning_allowed(bool provisioned, bool boot_button,
                                netmgr_state_t net_state);

/**
 * @brief Whether GET /api/io (relay/input state) may be served.
 *        Reading physical I/O state requires authentication after provisioning.
 */
bool webui_io_get_allowed(bool provisioned, bool has_auth);

/**
 * @brief Whether a mutating/control request (relay POST, OTA, config write) may
 *        proceed. Requires provisioned + authenticated + valid CSRF.
 */
bool webui_mutating_allowed(bool provisioned, bool has_auth, bool has_csrf);

#ifdef __cplusplus
}
#endif

#endif /* WEBUI_AUTHZ_H */
