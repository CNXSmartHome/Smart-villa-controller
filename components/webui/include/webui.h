/**
 * @file webui.h
 * @brief Local Web UI + REST API on esp_http_server.
 *
 * Serves a small static UI and a JSON REST API:
 *   GET  /              -> static UI (single page)
 *   GET  /api/status    -> firmware/uptime/network/presence summary
 *   GET  /api/io        -> relay + digital-input state
 *   POST /api/io        -> set a relay  (?relay=N&on=0|1)
 *   GET  /api/config    -> current configuration (secrets redacted)
 *   POST /api/ota       -> trigger OTA from a URL (optional)
 *
 * Read endpoints are fully implemented from live driver state. OTA and config
 * write are isolated, clearly-marked TODOs.
 */
#ifndef WEBUI_H
#define WEBUI_H

#include "svc_common.h"
#include "svc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP server and register handlers.
 * @param cfg Device config (for names / endpoints). Must be non-NULL.
 * @return SVC_OK or an error from esp_http_server.
 */
svc_err_t webui_start(const svc_config_t *cfg);

/** @brief Stop the HTTP server and release its socket(s). */
svc_err_t webui_stop(void);

/**
 * @brief Tell the Web UI whether the physical config button was held at boot.
 *
 * Must be called BEFORE webui_start(). When true, first-time provisioning is
 * permitted regardless of the post-boot factory window — this is the explicit,
 * physically-present provisioning path.
 *
 * @param held true if the config button was asserted during boot.
 */
void webui_set_provisioning_button(bool held);

#ifdef __cplusplus
}
#endif

#endif /* WEBUI_H */
