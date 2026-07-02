/**
 * @file webui_settings.h
 * @brief Pure config-write whitelist + apply for the Web UI settings page (SVC-014).
 *
 * The settings API (POST /api/config) may only write an EXPLICIT whitelist of
 * installer settings. Security-sensitive fields — board_id, version, crc,
 * provisioned, setup_password, webui_require_auth, relay polarity/safe-state —
 * are NOT writable here and are rejected (fail closed). Wi-Fi password is
 * accepted only as a normal body field (callers must never place it in a URL).
 *
 * No ESP-IDF: host-unit-tested. webui.c reads the request body and calls
 * webui_settings_apply() per key=value, then svc_config_sanitize() clamps
 * everything before it is persisted.
 */
#ifndef WEBUI_SETTINGS_H
#define WEBUI_SETTINGS_H

#include "svc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief True if @p key is an installer-settable field (whitelist). */
bool webui_settings_is_writable(const char *key);

/**
 * @brief True if @p key names a secret (wifi_pass, setup_password, mqtt_pass).
 *
 * Secrets may be WRITTEN via POST /api/config (body only) but must NEVER be
 * returned by GET /api/config. h_config_get emits an explicit non-secret field
 * list; this predicate documents and host-tests the write-only contract.
 */
bool webui_settings_is_secret(const char *key);

/**
 * @brief Apply one key=value pair to @p cfg if the key is writable.
 *
 * String values are bounded-copied; numeric values parsed with base-10. Values
 * are NOT range-checked here — the caller runs svc_config_sanitize() afterward,
 * which clamps/normalizes and disables anything unsafe.
 *
 * @return SVC_OK if applied; SVC_ERR_OUT_OF_RANGE if the key is unknown or a
 *         non-whitelisted (protected) field, rejected without modifying @p cfg.
 *         NOTE: the writable whitelist intentionally INCLUDES write-only secrets
 *         (wifi_pass, mqtt_pass) — they are accepted from the body but are never
 *         returned by GET (see webui_settings_is_secret).
 */
svc_err_t webui_settings_apply(svc_config_t *cfg, const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* WEBUI_SETTINGS_H */
