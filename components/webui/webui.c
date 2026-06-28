/**
 * @file webui.c
 * @brief Web UI + REST API implementation (see webui.h).
 *
 * Status/IO read endpoints are implemented from live driver state with a small
 * fixed stack buffer (no heap on the request path). Relay control parses query
 * args. OTA and config-write are clearly-marked TODO seams.
 */
#include "webui.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "relay.h"
#include "dinput.h"
#include "presence.h"
#include "svc_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "webui";

static httpd_handle_t s_server;
static svc_config_t   s_cfg;   /* snapshot for naming */

/* --- Minimal single-page UI (kept tiny; production assets live in /storage). --- */
static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SVC-100</title></head><body>"
    "<h2>Smart Villa Controller SVC-100</h2>"
    "<p>Local controller online. See <code>/api/status</code> and "
    "<code>/api/io</code>.</p>"
    "</body></html>";

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"product\":\"%s\",\"fw\":\"%s\",\"idf\":\"%s\","
        "\"uptime_ms\":%lu,\"presence\":%d,\"name\":\"%s\"}",
        SVC_PRODUCT_NAME, SVC_FW_VERSION, app ? app->idf_ver : "?",
        (unsigned long)svc_now_ms(), (int)presence_get(), s_cfg.device_name);
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    return send_json(req, buf);
}

static esp_err_t h_io_get(httpd_req_t *req)
{
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"relays\":%u,\"inputs\":%u}",
             relay_state_mask(), dinput_state_mask());
    return send_json(req, buf);
}

static esp_err_t h_io_post(httpd_req_t *req)
{
    char query[64];
    char vrelay[8], von[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "relay", vrelay, sizeof(vrelay)) != ESP_OK ||
        httpd_query_key_value(query, "on", von, sizeof(von)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"need ?relay=N&on=0|1\"}");
    }
    uint8_t relay = (uint8_t)atoi(vrelay);
    bool on = (atoi(von) != 0);
    svc_err_t rc = relay_set(relay, on);
    if (rc != SVC_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"set failed\"}");
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    /* Secrets (wifi_pass) intentionally redacted. */
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"name\":\"%s\",\"wifi\":%u,\"ssid\":\"%s\",\"eth\":%u,"
             "\"din_active_low\":%u,\"presence_slave\":%u}",
             s_cfg.device_name, s_cfg.wifi_enabled, s_cfg.wifi_ssid,
             s_cfg.eth_enabled, s_cfg.din_active_low, s_cfg.presence_slave);
    return send_json(req, buf);
}

static esp_err_t h_ota_post(httpd_req_t *req)
{
    /* TODO(ota): read JSON {"url":...}, run esp_https_ota(), then
       esp_ota_mark_app_valid_cancel_rollback() after self-check. */
    httpd_resp_set_status(req, "501 Not Implemented");
    return send_json(req, "{\"error\":\"ota not implemented yet\"}");
}

static esp_err_t register_routes(void)
{
    const httpd_uri_t routes[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status", .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/io",     .method = HTTP_GET,  .handler = h_io_get },
        { .uri = "/api/io",     .method = HTTP_POST, .handler = h_io_post },
        { .uri = "/api/config", .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/ota",    .method = HTTP_POST, .handler = h_ota_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        SVC_RETURN_ON_ERR(httpd_register_uri_handler(s_server, &routes[i]));
    }
    return SVC_OK;
}

svc_err_t webui_start(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);
    if (s_server != NULL) {
        return SVC_OK;
    }
    s_cfg = *cfg;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 12;
    hc.lru_purge_enable = true;
    hc.stack_size = 6144;
    hc.core_id = 0;

    SVC_RETURN_ON_ERR(httpd_start(&s_server, &hc));
    SVC_RETURN_ON_ERR(register_routes());
    ESP_LOGI(TAG, "web UI started on port %d", hc.server_port);
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
