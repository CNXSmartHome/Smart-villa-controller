/**
 * @file webui.c
 * @brief Web UI + REST API implementation (see webui.h).
 *
 * Security model (P1):
 *   - Physical output control is NEVER available to an unauthenticated client.
 *   - Mutating routes (relay control, config write, OTA) are DISABLED until the
 *     device is provisioned (a setup password has been established).
 *   - Once provisioned, every mutating request must pass BOTH:
 *       * authentication  — header `X-Auth-Token: <setup_password>`, and
 *       * CSRF protection  — header `X-SVC-CSRF: <per-boot token>` whose value a
 *         browser can only obtain from an authenticated GET /api/csrf. Because
 *         this is a non-standard request header, a cross-site form/script cannot
 *         set it without a CORS preflight that this server never approves.
 *   - First-time provisioning (POST /api/provision) is accepted only while the
 *     device is un-provisioned, and is intended to be reachable over the local
 *     AP / LAN only.
 *
 * JSON safety (P2): every configuration-derived string placed in a response is
 * passed through svc_json_escape(); raw strings are never concatenated in.
 *
 * Init cleanup (P7): if route registration fails, the server is stopped so no
 * half-initialized listener is left running.
 */
#include "webui.h"
#include "svc_json.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "relay.h"
#include "dinput.h"
#include "presence.h"
#include "eventbus.h"
#include "health.h"
#include "storage.h"
#include "control.h"
#include "netmgr.h"
#include "webui_authz.h"
#include "svc_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "webui";

static httpd_handle_t s_server;
static svc_config_t   s_cfg;            /* live working copy */
static char           s_csrf[33];       /* per-boot CSRF token (hex) */
static bool           s_boot_button;    /* config button held at boot */

/* --- Minimal single-page UI (production assets live in the /storage part). --- */
static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SVC-100</title></head><body>"
    "<h2>Smart Villa Controller SVC-100</h2>"
    "<p>Local controller. Status at <code>/api/status</code>. Control endpoints "
    "require setup + authentication.</p>"
    "</body></html>";

/* ----------------------------------------------------------------- helpers */

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_status(httpd_req_t *req, const char *code, const char *json)
{
    httpd_resp_set_status(req, code);
    return send_json(req, json);
}

/** Read a request header into @p buf; returns true if present and it fit. */
static bool get_header(httpd_req_t *req, const char *name, char *buf, size_t cap)
{
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len >= cap) {
        return false;
    }
    return httpd_req_get_hdr_value_str(req, name, buf, cap) == ESP_OK;
}

/** Length-independent compare to avoid trivial timing leaks on the token. */
static bool str_eq_ct(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; ++i) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static bool is_provisioned(void)
{
    return s_cfg.provisioned && s_cfg.setup_password[0] != '\0';
}

/**
 * @brief Authentication check: valid setup token in X-Auth-Token.
 *
 * SECURITY: authentication is MANDATORY and can never be disabled by config.
 * The webui_require_auth field is deliberately ignored here — a forged config
 * that clears it must not be able to open up control. Once the device is
 * provisioned (non-empty setup password), every protected request must present
 * the matching token.
 */
static bool api_authenticated(httpd_req_t *req)
{
    if (s_cfg.setup_password[0] == '\0') {
        /* No password established => not provisioned => no authenticated state
           exists. Mutating routes are blocked separately by is_provisioned(). */
        return false;
    }
    char token[SVC_SETUP_PW_MAX + 1];
    if (!get_header(req, "X-Auth-Token", token, sizeof(token))) {
        return false;
    }
    return str_eq_ct(token, s_cfg.setup_password);
}

/**
 * @brief Whether first-time provisioning is currently permitted.
 *
 * Delegates to the host-tested webui_provisioning_allowed() predicate: never
 * once provisioned, otherwise ONLY with the physical config button at boot or
 * active AP provisioning mode. There is no ambient-LAN time window.
 */
static bool provisioning_mode(void)
{
    return webui_provisioning_allowed(is_provisioned(), s_boot_button,
                                      netmgr_state());
}

/** CSRF check: per-boot token in the non-standard X-SVC-CSRF header. */
static bool csrf_ok(httpd_req_t *req)
{
    char token[sizeof(s_csrf)];
    if (!get_header(req, "X-SVC-CSRF", token, sizeof(token))) {
        return false;
    }
    return str_eq_ct(token, s_csrf);
}

/**
 * @brief Gate for any mutating/control request.
 * @return ESP_OK if allowed; otherwise a 4xx JSON error was already sent.
 */
static esp_err_t guard_mutating(httpd_req_t *req)
{
    bool prov   = is_provisioned();
    bool authed = api_authenticated(req);
    bool csrf   = csrf_ok(req);

    /* Granular status codes for the client, but the final allow/deny decision
       is the host-tested webui_mutating_allowed() predicate. */
    if (!prov) {
        ESP_LOGW(TAG, "mutating request rejected: not provisioned");
        send_status(req, "403 Forbidden",
                    "{\"error\":\"device not provisioned; setup required\"}");
        return ESP_FAIL;
    }
    if (!authed) {
        send_status(req, "401 Unauthorized",
                    "{\"error\":\"authentication required\"}");
        return ESP_FAIL;
    }
    if (!csrf) {
        send_status(req, "403 Forbidden", "{\"error\":\"missing/invalid CSRF\"}");
        return ESP_FAIL;
    }
    return webui_mutating_allowed(prov, authed, csrf) ? ESP_OK : ESP_FAIL;
}

/* ----------------------------------------------------------------- handlers */

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    health_report_t hr;
    health_get(&hr);

    char name_esc[SVC_DEVICE_NAME_MAX * 6 + 1];
    /* strnlen bounds the read to the fixed array even if it is not terminated. */
    if (svc_json_escape_n(s_cfg.device_name,
                          strnlen(s_cfg.device_name, sizeof(s_cfg.device_name)),
                          name_esc, sizeof(name_esc)) != SVC_OK) {
        name_esc[0] = '\0';
    }

    char buf[420];
    int n = snprintf(buf, sizeof(buf),
        "{\"product\":\"%s\",\"fw\":\"%s\",\"idf\":\"%s\",\"name\":\"%s\","
        "\"uptime_ms\":%lu,\"presence\":%d,\"provisioned\":%s,"
        "\"health\":\"%s\",\"fault\":%s,\"events_dropped\":%lu}",
        SVC_PRODUCT_NAME, SVC_FW_VERSION, app ? app->idf_ver : "?", name_esc,
        (unsigned long)svc_now_ms(), (int)presence_get(),
        is_provisioned() ? "true" : "false",
        health_status_str(hr.status), hr.fault_latched ? "true" : "false",
        (unsigned long)eventbus_dropped_count());
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    return send_json(req, buf);
}

static esp_err_t h_io_get(httpd_req_t *req)
{
    /* Reading physical I/O state is privileged: it requires authentication just
       like control does. An un-provisioned/unauthenticated client gets 401. */
    if (!webui_io_get_allowed(is_provisioned(), api_authenticated(req))) {
        return send_status(req, "401 Unauthorized",
                           "{\"error\":\"authentication required\"}");
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"relays\":%u,\"inputs\":%u}",
             relay_state_mask(), dinput_state_mask());
    return send_json(req, buf);
}

/**
 * @brief Retrieve the per-boot CSRF token.
 *
 * Available to an authenticated client (normal operation) OR while the device
 * is in provisioning mode (so the setup page can obtain a token before any
 * password exists). A cross-site attacker cannot read this response due to the
 * same-origin policy, so the token stays confidential to the device's own UI.
 */
static esp_err_t h_csrf_get(httpd_req_t *req)
{
    if (!(provisioning_mode() || api_authenticated(req))) {
        return send_status(req, "401 Unauthorized",
                           "{\"error\":\"authentication required\"}");
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"csrf\":\"%s\"}", s_csrf);
    return send_json(req, buf);
}

static esp_err_t h_io_post(httpd_req_t *req)
{
    if (guard_mutating(req) != ESP_OK) {
        return ESP_OK;   /* response already sent by the gate */
    }
    char query[64], vrelay[8], von[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "relay", vrelay, sizeof(vrelay)) != ESP_OK ||
        httpd_query_key_value(query, "on", von, sizeof(von)) != ESP_OK) {
        return send_status(req, "400 Bad Request",
                           "{\"error\":\"need ?relay=N&on=0|1\"}");
    }
    uint8_t relay = (uint8_t)atoi(vrelay);
    bool on = (atoi(von) != 0);
    if (relay_set(relay, on) != SVC_OK) {
        return send_status(req, "400 Bad Request", "{\"error\":\"set failed\"}");
    }
    ESP_LOGW(TAG, "authenticated relay %u -> %d", relay, on);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    /* Secrets (wifi_pass, setup_password) are NEVER returned. Each config string
       is escaped with svc_json_escape_n bounded by strnlen over the fixed array,
       so an un-terminated array can never cause an out-of-bounds read. */
    char name_esc[SVC_DEVICE_NAME_MAX * 6 + 1];
    char ssid_esc[SVC_WIFI_SSID_MAX * 6 + 1];
    if (svc_json_escape_n(s_cfg.device_name,
                          strnlen(s_cfg.device_name, sizeof(s_cfg.device_name)),
                          name_esc, sizeof(name_esc)) != SVC_OK)
        name_esc[0] = '\0';
    if (svc_json_escape_n(s_cfg.wifi_ssid,
                          strnlen(s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid)),
                          ssid_esc, sizeof(ssid_esc)) != SVC_OK)
        ssid_esc[0] = '\0';

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"name\":\"%s\",\"wifi\":%u,\"ssid\":\"%s\",\"eth\":%u,"
             "\"din_active_low\":%u,\"presence_slave\":%u,\"provisioned\":%s}",
             name_esc, s_cfg.wifi_enabled, ssid_esc, s_cfg.eth_enabled,
             s_cfg.din_active_low, s_cfg.presence_slave,
             is_provisioned() ? "true" : "false");
    return send_json(req, buf);
}

/**
 * @brief Extract a form field value from an application/x-www-form-urlencoded
 *        request body. Bounded; does not require a NUL-terminated body.
 * @return true if @p key was found and copied into @p out.
 */
static bool form_field(const char *body, size_t body_len, const char *key,
                       char *out, size_t out_cap)
{
    size_t klen = strlen(key);
    for (size_t i = 0; i + klen + 1 <= body_len; ) {
        /* match "key=" at a field boundary (start or just after '&') */
        bool at_boundary = (i == 0) || (body[i - 1] == '&');
        if (at_boundary && strncmp(&body[i], key, klen) == 0 &&
            body[i + klen] == '=') {
            size_t v = i + klen + 1, o = 0;
            while (v < body_len && body[v] != '&' && o + 1 < out_cap) {
                out[o++] = body[v++];
            }
            out[o] = '\0';
            return true;
        }
        /* advance to next '&' */
        while (i < body_len && body[i] != '&') i++;
        while (i < body_len && body[i] == '&') i++;
    }
    return false;
}

/**
 * @brief First-time provisioning: establish the setup password.
 *
 * SECURITY (P2 of this pass):
 *   - Only accepted while provisioning_mode() is true (button-at-boot / AP mode
 *     / factory window) — never from ambient LAN on an already-running unit.
 *   - The password is read from the POST BODY only, never the URL query string
 *     (query strings leak into logs, history, and the Referer header).
 *   - A valid per-boot CSRF header is required (browser-originated protection).
 */
static esp_err_t h_provision_post(httpd_req_t *req)
{
    if (is_provisioned()) {
        return send_status(req, "409 Conflict",
                           "{\"error\":\"already provisioned\"}");
    }
    if (!provisioning_mode()) {
        ESP_LOGW(TAG, "provision rejected: not in provisioning mode");
        return send_status(req, "403 Forbidden",
                           "{\"error\":\"provisioning mode not active\"}");
    }
    if (!csrf_ok(req)) {
        return send_status(req, "403 Forbidden", "{\"error\":\"missing/invalid CSRF\"}");
    }

    /* Read the body (bounded). Reject oversized bodies outright. */
    char body[160];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        return send_status(req, "400 Bad Request",
                           "{\"error\":\"missing/oversized body\"}");
    }
    int total = 0;
    while (total < (int)req->content_len) {
        int r = httpd_req_recv(req, body + total, sizeof(body) - 1 - total);
        if (r <= 0) {
            return send_status(req, "400 Bad Request", "{\"error\":\"body read failed\"}");
        }
        total += r;
    }
    body[total] = '\0';

    char pw[SVC_SETUP_PW_MAX];
    if (!form_field(body, (size_t)total, "password", pw, sizeof(pw)) ||
        strlen(pw) < 8) {
        return send_status(req, "400 Bad Request",
                           "{\"error\":\"password (>= 8 chars) required in body\"}");
    }

    strncpy(s_cfg.setup_password, pw, sizeof(s_cfg.setup_password) - 1);
    s_cfg.setup_password[sizeof(s_cfg.setup_password) - 1] = '\0';
    s_cfg.provisioned = 1;
    s_cfg.webui_require_auth = 1;
    if (storage_save(&s_cfg) != SVC_OK) {
        s_cfg.provisioned = 0;
        s_cfg.setup_password[0] = '\0';
        return send_status(req, "500 Internal Server Error",
                           "{\"error\":\"persist failed\"}");
    }
    (void)control_reload_config(&s_cfg);
    ESP_LOGW(TAG, "device provisioned; mutating routes now require auth+CSRF");
    return send_json(req, "{\"ok\":true,\"provisioned\":true}");
}

static esp_err_t h_ota_post(httpd_req_t *req)
{
    if (guard_mutating(req) != ESP_OK) {
        return ESP_OK;
    }
    /* TODO(ota): read JSON {"url":...}, run esp_https_ota(), then mark valid via
       the health gate after self-check. Gated identically to relay control. */
    return send_status(req, "501 Not Implemented",
                       "{\"error\":\"ota not implemented yet\"}");
}

/* ----------------------------------------------------------------- lifecycle */

static svc_err_t register_routes(void)
{
    const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/io",        .method = HTTP_GET,  .handler = h_io_get },
        { .uri = "/api/io",        .method = HTTP_POST, .handler = h_io_post },
        { .uri = "/api/config",    .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/csrf",      .method = HTTP_GET,  .handler = h_csrf_get },
        { .uri = "/api/provision", .method = HTTP_POST, .handler = h_provision_post },
        { .uri = "/api/ota",       .method = HTTP_POST, .handler = h_ota_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        SVC_RETURN_ON_ERR(httpd_register_uri_handler(s_server, &routes[i]));
    }
    return SVC_OK;
}

static void csrf_token_generate(void)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));
    for (size_t i = 0; i < sizeof(rnd); ++i) {
        s_csrf[i * 2]     = hex[(rnd[i] >> 4) & 0xF];
        s_csrf[i * 2 + 1] = hex[rnd[i] & 0xF];
    }
    s_csrf[sizeof(s_csrf) - 1] = '\0';
}

svc_err_t webui_start(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (s_server != NULL) {
        return SVC_OK;
    }
    s_cfg = *cfg;
    csrf_token_generate();

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 16;
    hc.lru_purge_enable = true;
    hc.stack_size = 6144;
    hc.core_id = 0;

    SVC_RETURN_ON_ERR(httpd_start(&s_server, &hc));

    /* P7: if routes fail to register, tear the server down — no half-open state. */
    svc_err_t rc = register_routes();
    if (rc != SVC_OK) {
        ESP_LOGE(TAG, "route registration failed (0x%x); stopping server", (int)rc);
        httpd_stop(s_server);
        s_server = NULL;
        return rc;
    }
    ESP_LOGI(TAG, "web UI started on port %d (provisioned=%s)",
             hc.server_port, is_provisioned() ? "yes" : "no");
    return SVC_OK;
}

svc_err_t webui_stop(void)
{
    if (s_server == NULL) {
        return SVC_OK;
    }
    svc_err_t rc = httpd_stop(s_server);
    s_server = NULL;
    return rc;
}

void webui_set_provisioning_button(bool held)
{
    s_boot_button = held;
    if (held) {
        ESP_LOGW(TAG, "config button held at boot: provisioning mode enabled");
    }
}
