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
#include "hal_board.h"
#include "netmgr.h"
#include "webui_authz.h"
#include "webui_settings.h"
#include "svc_version.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const char *TAG = "webui";

static httpd_handle_t s_server;
static svc_config_t   s_cfg;            /* live working copy */
static char           s_csrf[33];       /* per-boot CSRF token (hex) */
static bool           s_boot_button;    /* config button held at boot */

/* --- Installer settings page (SVC-014). Single-file; the setup password is
 * held only in memory and sent as the X-Auth-Token header — never in a URL. --- */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>SVC-100 Settings</title><style>"
"body{font-family:sans-serif;max-width:640px;margin:1rem auto;padding:0 1rem}"
"label{display:block;margin:.5rem 0 .2rem;font-size:.9rem}"
"input,select{width:100%;padding:.4rem;box-sizing:border-box}"
"fieldset{margin:.8rem 0}button{padding:.5rem 1rem;margin-top:.6rem}"
"#msg{margin:.6rem 0;font-weight:bold}</style></head><body>"
"<h2>Smart Villa Controller — Settings</h2>"
"<div id=msg></div>"
"<fieldset><legend>Sign in</legend>"
"<label>Setup password</label><input id=pw type=password autocomplete=off>"
"<button onclick=load()>Load settings</button></fieldset>"
"<form id=f style=display:none>"
"<fieldset><legend>Device</legend>"
"<label>Device name</label><input name=device_name maxlength=31>"
"<label>Room empty delay (sec)</label><input name=room_empty_delay_sec type=number min=0 max=3600>"
"<label>Sensor fault policy</label><select name=sensor_fault_policy>"
"<option value=0>Hold (never false-off)</option><option value=1>Assume occupied</option>"
"<option value=2>Ignore faulted</option></select></fieldset>"
"<fieldset><legend>Wi-Fi</legend>"
"<label>Enabled</label><select name=wifi_enabled><option value=0>No</option><option value=1>Yes</option></select>"
"<label>SSID</label><input name=wifi_ssid maxlength=31>"
"<label>Password</label><input name=wifi_pass type=password autocomplete=off maxlength=63></fieldset>"
"<fieldset><legend>Presence sensors</legend>"
"<label>Sensor count</label><select name=presence_sensor_count><option>0</option><option>1</option><option>2</option></select>"
"<label>Sensor 1 type</label><select name=presence_1_type><option value=0>Disabled</option><option value=1>RS485</option><option value=2>Dry contact</option></select>"
"<label>S1 RS485 port</label><input name=presence_1_rs485_port type=number min=0>"
"<label>S1 Modbus addr</label><input name=presence_1_modbus_addr type=number min=1 max=247>"
"<label>S1 dry-contact input</label><input name=presence_1_din_index type=number min=0>"
"<label>Sensor 2 type</label><select name=presence_2_type><option value=0>Disabled</option><option value=1>RS485</option><option value=2>Dry contact</option></select>"
"<label>S2 RS485 port</label><input name=presence_2_rs485_port type=number min=0>"
"<label>S2 Modbus addr</label><input name=presence_2_modbus_addr type=number min=1 max=247>"
"<label>S2 dry-contact input</label><input name=presence_2_din_index type=number min=0>"
"</fieldset><button type=button onclick=save()>Save settings</button></form>"
"<script>"
"var TOK='',CSRF='';"
"function H(){return {'X-Auth-Token':TOK,'X-SVC-CSRF':CSRF}}"
"function m(t,ok){var e=document.getElementById('msg');e.textContent=t;e.style.color=ok?'green':'crimson'}"
"async function load(){TOK=document.getElementById('pw').value;m('Loading...',true);"
"try{var c=await fetch('/api/csrf',{headers:{'X-Auth-Token':TOK}});"
"if(!c.ok){m('Auth failed',false);return}CSRF=(await c.json()).csrf;"
"var r=await fetch('/api/config',{headers:{'X-Auth-Token':TOK}});"
"if(!r.ok){m('Load failed: '+r.status,false);return}var cfg=await r.json();"
"var f=document.getElementById('f');f.style.display='block';"
"for(var k in cfg){if(f[k]!==undefined)f[k].value=cfg[k]}"
"m('Loaded. Edit and Save.',true)}catch(e){m('Error: '+e,false)}}"
"async function save(){var f=document.getElementById('f');var b=[];"
"for(var el of f.elements){if(el.name&&el.value!=='')"
"b.push(encodeURIComponent(el.name)+'='+encodeURIComponent(el.value))}"
"try{var r=await fetch('/api/config',{method:'POST',"
"headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H()),"
"body:b.join('&')});var j=await r.json();"
"if(r.ok)m('Saved ('+j.applied+' fields).',true);else m('Save failed: '+(j.error||r.status),false)}"
"catch(e){m('Error: '+e,false)}}"
"</script></body></html>";

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

/**
 * @brief Bounded JSON/text append into @p buf, tracking the write position in
 *        @p *pos. Safe against truncation: if the formatted output does not fit,
 *        @p *pos is saturated to @p cap and the function returns false so the
 *        caller stops appending. It NEVER computes an out-of-bounds pointer
 *        (`buf + *pos`) or an underflowing remaining-size (`cap - *pos`),
 *        because it early-returns once @p *pos == cap.
 *
 * Usage: check the final `*pos < cap` (or the accumulated bool) before sending;
 * a saturated buffer means the response was too large and must be a 500 rather
 * than silently-truncated JSON.
 */
static bool buf_appendf(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (*pos >= cap) {
        return false;                     /* already full: no OOB pointer/size */
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= cap - *pos) {
        *pos = cap;                       /* encoding error or truncation */
        return false;
    }
    *pos += (size_t)w;
    return true;
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

/**
 * @brief Length-independent equality compare, to avoid timing leaks on a secret.
 *
 * The loop ALWAYS runs @p span iterations and unconditionally reads BOTH
 * buffers at every index — no strlen() pre-scan, no length-dependent branch
 * anywhere in the compare path — so the time taken reveals neither the
 * secret's length nor the matching-prefix length. Bytes at or past each
 * string's NUL are masked to 0 by a branchlessly-maintained "live" mask, and
 * any live-mask divergence (a length difference) is folded into the result,
 * so unequal-length strings compare false without early exit.
 *
 * @p span must be >= the secret buffer's capacity (callers pass
 * sizeof(secret)), and BOTH pointers must reference buffers of at least
 * @p span readable bytes: every index < span is read unconditionally.
 * Callers holding a possibly-shorter string must use a buffer of at least
 * @p span capacity (zero-initialize it so trailing bytes are defined).
 */
static bool str_eq_ct(const char *a, const char *b, size_t span)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    unsigned char diff   = 0;
    unsigned char a_live = 0xFF;   /* 0xFF until a's NUL has been seen */
    unsigned char b_live = 0xFF;   /* 0xFF until b's NUL has been seen */
    for (size_t i = 0; i < span; ++i) {
        unsigned char ca = (unsigned char)a[i] & a_live;
        unsigned char cb = (unsigned char)b[i] & b_live;
        diff |= (unsigned char)(ca ^ cb);         /* content mismatch */
        diff |= (unsigned char)(a_live ^ b_live); /* length mismatch  */
        /* Branchless mask update: ((c + 0xFF) >> 8) is 1 iff c != 0, so the
           mask stays 0xFF inside the string and drops to 0 at/after the NUL. */
        a_live &= (unsigned char)(0u - (((unsigned)ca + 0xFFu) >> 8));
        b_live &= (unsigned char)(0u - (((unsigned)cb + 0xFFu) >> 8));
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
    char token[SVC_SETUP_PW_MAX + 1] = {0};  /* zeroed: str_eq_ct reads span bytes */
    if (!get_header(req, "X-Auth-Token", token, sizeof(token))) {
        return false;
    }
    return str_eq_ct(token, s_cfg.setup_password, sizeof(s_cfg.setup_password));
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
    char token[sizeof(s_csrf)] = {0};        /* zeroed: str_eq_ct reads span bytes */
    if (!get_header(req, "X-SVC-CSRF", token, sizeof(token))) {
        return false;
    }
    return str_eq_ct(token, s_csrf, sizeof(s_csrf));
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

/* Effective I/O bounds: prefer the installed HAL profile, but fall back to
 * the legacy compile-time board limits while app_main still boots the pre-HAL
 * path (until SVC-024/025 wire hal_install()/hal_board_init()). Without the
 * fallback hal_relay_count() returns 0 and every authenticated relay POST
 * would be rejected as out of range. The fallback cannot widen access: the
 * relay/dinput drivers still fail closed at BOARD_*_COUNT internally. */
static uint8_t effective_relay_count(void)
{
    uint8_t n = hal_relay_count();
    return n ? n : BOARD_RELAY_COUNT;
}

static uint8_t effective_din_count(void)
{
    uint8_t n = hal_din_count();
    return n ? n : BOARD_DINPUT_COUNT;
}

static esp_err_t h_io_get(httpd_req_t *req)
{
    /* Reading physical I/O state is privileged: it requires authentication just
       like control does. An un-provisioned/unauthenticated client gets 401. */
    if (!webui_io_get_allowed(is_provisioned(), api_authenticated(req))) {
        return send_status(req, "401 Unauthorized",
                           "{\"error\":\"authentication required\"}");
    }
    /* Board-aware status: identity, counts, capabilities, labels, live states.
       Counts fall back to the compile-time board limits while HAL is unwired
       so the UI still renders the relays (see effective_relay_count()). */
    const board_profile_t *bp = hal_board_get_info();
    uint8_t rc = effective_relay_count();
    uint8_t dc = effective_din_count();
    uint32_t rmask = relay_state_mask();
    uint32_t imask = dinput_state_mask();

    char buf[1024];
    size_t pos = 0;
    bool ok = true;
    char nm[SVC_DEVICE_NAME_MAX * 6 + 1];
    if (svc_json_escape_n(s_cfg.device_name,
                          strnlen(s_cfg.device_name, sizeof(s_cfg.device_name)),
                          nm, sizeof(nm)) != SVC_OK) nm[0] = '\0';
    /* P2 (Project Bible): every profile-derived string is escaped before it
       is placed in JSON — identity and labels included, not just config. On
       escape failure (oversized input) the field degrades to "" rather than
       emitting raw or truncated text. board_id is a fixed array (bounded via
       strnlen); board_name/labels are NUL-terminated string literals. */
    char id_esc[HAL_BOARD_ID_MAX * 6 + 1];
    char bn_esc[48 * 6 + 1];
    char lbl_esc[32 * 6 + 1];
    if (!bp || svc_json_escape_n(bp->board_id,
                                 strnlen(bp->board_id, sizeof(bp->board_id)),
                                 id_esc, sizeof(id_esc)) != SVC_OK)
        id_esc[0] = '\0';
    if (!bp || svc_json_escape(bp->board_name ? bp->board_name : "",
                               bn_esc, sizeof(bn_esc)) != SVC_OK)
        bn_esc[0] = '\0';
    ok &= buf_appendf(buf, sizeof(buf), &pos,
        "{\"board_id\":\"%s\",\"board_name\":\"%s\",\"device_name\":\"%s\","
        "\"capabilities\":%lu,\"relay_count\":%u,\"input_count\":%u,"
        "\"relays\":%lu,\"inputs\":%lu,\"relay_labels\":[",
        id_esc, bn_esc, nm,
        (unsigned long)hal_board_get_capabilities(), rc, dc,
        (unsigned long)rmask, (unsigned long)imask);
    for (uint8_t i = 0; i < rc && ok; ++i) {
        const char *lbl = (bp && bp->relay_labels) ? bp->relay_labels[i] : "";
        if (svc_json_escape(lbl ? lbl : "", lbl_esc, sizeof(lbl_esc)) != SVC_OK)
            lbl_esc[0] = '\0';
        ok &= buf_appendf(buf, sizeof(buf), &pos, "%s\"%s\"", i ? "," : "", lbl_esc);
    }
    ok &= buf_appendf(buf, sizeof(buf), &pos, "],\"input_labels\":[");
    for (uint8_t i = 0; i < dc && ok; ++i) {
        const char *lbl = (bp && bp->din_labels) ? bp->din_labels[i] : "";
        if (svc_json_escape(lbl ? lbl : "", lbl_esc, sizeof(lbl_esc)) != SVC_OK)
            lbl_esc[0] = '\0';
        ok &= buf_appendf(buf, sizeof(buf), &pos, "%s\"%s\"", i ? "," : "", lbl_esc);
    }
    ok &= buf_appendf(buf, sizeof(buf), &pos, "]}");
    if (!ok) {                            /* truncated -> never send partial JSON */
        return httpd_resp_send_500(req);
    }
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
    int ri = atoi(vrelay);
    bool on = (atoi(von) != 0);
    /* Fail closed: reject out-of-range / unavailable relay indices for this board. */
    if (ri < 0 || ri >= (int)effective_relay_count()) {
        return send_status(req, "400 Bad Request",
                           "{\"error\":\"relay index out of range for this board\"}");
    }
    if (relay_set((uint8_t)ri, on) != SVC_OK) {
        return send_status(req, "400 Bad Request", "{\"error\":\"set failed\"}");
    }
    ESP_LOGW(TAG, "authenticated relay %d -> %d", ri, on);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    /* Installer config (SSID, enabled interfaces, sensor topology, input polarity,
       provisioning state) is privileged. Same rule as GET /api/io: after
       provisioning it REQUIRES authentication. It never returns secrets, but is
       not public. The settings page already sends X-Auth-Token. */
    if (!webui_io_get_allowed(is_provisioned(), api_authenticated(req))) {
        return send_status(req, "401 Unauthorized",
                           "{\"error\":\"authentication required\"}");
    }

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

    /* Field names match the settings-form inputs and the writable whitelist so
       the page can populate directly (secrets excluded). Extra keys the form
       does not bind are harmlessly ignored by the loader. Buffer sized for the
       worst case (fully-escaped name+ssid, ~6x each) and the write is checked:
       a truncated response is a 500, never silently-truncated JSON. */
    const presence_sensor_cfg_t *s1 = &s_cfg.presence_sensor[0];
    const presence_sensor_cfg_t *s2 = &s_cfg.presence_sensor[1];
    char buf[768];
    size_t pos = 0;
    bool ok = buf_appendf(buf, sizeof(buf), &pos,
             "{\"device_name\":\"%s\",\"wifi_enabled\":%u,\"wifi_ssid\":\"%s\","
             "\"eth_enabled\":%u,\"din_active_low\":%" PRIu32 ","
             "\"room_empty_delay_sec\":%u,\"sensor_fault_policy\":%u,"
             "\"presence_sensor_count\":%u,"
             "\"presence_1_type\":%u,\"presence_1_rs485_port\":%u,"
             "\"presence_1_modbus_addr\":%u,\"presence_1_din_index\":%u,"
             "\"presence_2_type\":%u,\"presence_2_rs485_port\":%u,"
             "\"presence_2_modbus_addr\":%u,\"presence_2_din_index\":%u,"
             "\"provisioned\":%s}",
             name_esc, s_cfg.wifi_enabled, ssid_esc, s_cfg.eth_enabled,
             s_cfg.din_active_low, s_cfg.room_empty_delay_sec,
             s_cfg.sensor_fault_policy, s_cfg.presence_sensor_count,
             s1->type, s1->rs485_port, s1->modbus_addr, s1->din_index,
             s2->type, s2->rs485_port, s2->modbus_addr, s2->din_index,
             is_provisioned() ? "true" : "false");
    if (!ok) {
        return httpd_resp_send_500(req);
    }
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
 *   - Only accepted while provisioning_mode() is true, i.e. ONLY with the
 *     physical config button held at boot or active AP provisioning mode. There
 *     is no LAN/time-window path — ambient-LAN provisioning is not possible.
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

/** Minimal URL-decode (%XX + '+') into a bounded buffer. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void url_decode(char *dst, size_t cap, const char *src, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < cap; ++i) {
        char c = src[i];
        if (c == '+') {
            dst[o++] = ' ';
        } else if (c == '%' && i + 2 < len) {
            int hi = hexval(src[i + 1]), lo = hexval(src[i + 2]);
            if (hi >= 0 && lo >= 0) { dst[o++] = (char)(hi * 16 + lo); i += 2; }
            else dst[o++] = c;
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

/**
 * @brief SVC-014 settings write. Mutating -> requires provisioned + auth + CSRF.
 *        Body is application/x-www-form-urlencoded key=value pairs (secrets like
 *        wifi_pass travel in the BODY, never the URL). Only whitelisted installer
 *        keys are applied (webui_settings_apply); the config is sanitized and
 *        persisted atomically (a working copy is committed only if it saves).
 */
static esp_err_t h_config_post(httpd_req_t *req)
{
    if (guard_mutating(req) != ESP_OK) {
        return ESP_OK;   /* 401/403 already sent */
    }
    /* Sized for the full settings form fully percent-encoded: worst case is
       ~700 bytes (device_name 31 + ssid 31 + wifi_pass 63, all %XX-expanded,
       plus ~15 keys). 512 was too small and would 400 a legitimate save. */
    char body[1024];
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

    svc_config_t work = s_cfg;   /* apply to a copy; commit only on success */
    int applied = 0, rejected = 0;
    size_t i = 0, len = (size_t)total;
    while (i < len) {
        size_t start = i;
        while (i < len && body[i] != '&') i++;
        size_t tlen = i - start;
        if (i < len) i++;                       /* skip '&' */
        const char *tok = &body[start];
        size_t eq = 0;
        while (eq < tlen && tok[eq] != '=') eq++;
        if (eq == tlen) continue;               /* malformed pair */
        char key[48], val[96];
        url_decode(key, sizeof(key), tok, eq);
        url_decode(val, sizeof(val), tok + eq + 1, tlen - eq - 1);
        if (webui_settings_apply(&work, key, val) == SVC_OK) applied++;
        else rejected++;                         /* unknown/protected -> ignored */
    }

    /* Sanitize enforces all bounds + forces auth-on-when-provisioned, so a
       settings write can never weaken security or write an unsafe value. */
    svc_config_sanitize(&work);
    if (storage_save(&work) != SVC_OK) {
        return send_status(req, "500 Internal Server Error",
                           "{\"error\":\"persist failed\"}");
    }
    s_cfg = work;
    (void)control_reload_config(&s_cfg);
    char buf[96];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ok\":true,\"applied\":%d,\"rejected\":%d}",
                     applied, rejected);
    if (n < 0 || n >= (int)sizeof(buf)) {   /* provably fits; checked for parity */
        return httpd_resp_send_500(req);
    }
    ESP_LOGW(TAG, "settings updated (applied=%d rejected=%d)", applied, rejected);
    return send_json(req, buf);
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
        { .uri = "/api/config",    .method = HTTP_POST, .handler = h_config_post },
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
