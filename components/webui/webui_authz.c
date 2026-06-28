/**
 * @file webui_authz.c
 * @brief Pure authorization decisions for the Web UI (see webui_authz.h).
 *
 * No esp_http_server / hardware dependency, so this is unit-tested on the host.
 */
#include "webui_authz.h"

bool webui_provisioning_allowed(bool provisioned, bool boot_button,
                                netmgr_state_t net_state)
{
    if (provisioned) {
        return false;                 /* never re-provision silently */
    }
    if (boot_button) {
        return true;                  /* physically-present operator */
    }
    if (net_state == NET_AP_PROVISIONING) {
        return true;                  /* explicit AP provisioning mode */
    }
    return false;                     /* NO ambient-LAN time window */
}

bool webui_io_get_allowed(bool provisioned, bool has_auth)
{
    return provisioned && has_auth;
}

bool webui_mutating_allowed(bool provisioned, bool has_auth, bool has_csrf)
{
    return provisioned && has_auth && has_csrf;
}
