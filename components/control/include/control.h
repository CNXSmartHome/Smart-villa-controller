/**
 * @file control.h
 * @brief Standalone control engine: consumes the event bus and drives relays.
 *
 * This is the device's brain and the only mandatory service. It evaluates the
 * configured rule table (presence + digital-input edges, with per-rule off
 * delays), applies failsafe on critical source loss, and reacts to button
 * events (factory reset / provisioning). It depends on no networking.
 */
#ifndef CONTROL_H
#define CONTROL_H

#include "svc_common.h"
#include "svc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the control task (subscribes to the Task Watchdog).
 *
 * Must be called after eventbus_init(), relay_init(), indicator_start(), and
 * after the configuration has been loaded. The task takes a private copy of the
 * config; call control_reload_config() after a config change.
 *
 * @param cfg Loaded device config. Must be non-NULL.
 * @return SVC_OK or an error from the RTOS/watchdog layer.
 */
svc_err_t control_start(const svc_config_t *cfg);

/** @brief Re-read configuration (e.g. after a Web UI change). */
svc_err_t control_reload_config(const svc_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_H */
