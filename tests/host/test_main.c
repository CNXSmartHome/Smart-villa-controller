/**
 * @file test_main.c
 * @brief Host unit tests for the SVC-100 pure-logic units.
 *
 * Compiles and exercises the ACTUAL shipped sources where they have no hardware
 * dependency: svc_json.c (JSON escaping) and dinput_debounce.h (debounce). Also
 * re-verifies the Modbus RTU CRC vector and the Modbus response-length rules and
 * the config-migration version/size decision used by storage.c.
 *
 * Build & run:  tests/host/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <stdlib.h>

#include "svc_json.h"               /* real header */
#include "dinput_debounce.h"        /* real pure-logic header */
#include "svc_config.h"             /* real config schema + sanitizer */
#include "webui_authz.h"            /* real Web UI gate predicates */

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail++; } \
    else         { printf("  ok  : %s\n", msg); } } while (0)

/* ---- P2: JSON escaping (shipped svc_json_escape) ---- */
static void test_json_escape(void)
{
    printf("[json escape]\n");
    char out[128];

    CHECK(svc_json_escape("plain", out, sizeof(out)) == SVC_OK &&
          strcmp(out, "plain") == 0, "plain passes through");

    /* The injection cases: quote and backslash must be escaped. */
    CHECK(svc_json_escape("a\"b", out, sizeof(out)) == SVC_OK &&
          strcmp(out, "a\\\"b") == 0, "double-quote escaped");
    CHECK(svc_json_escape("a\\b", out, sizeof(out)) == SVC_OK &&
          strcmp(out, "a\\\\b") == 0, "backslash escaped");

    /* An attacker-style name trying to break out of the JSON string. */
    CHECK(svc_json_escape("\",\"admin\":true", out, sizeof(out)) == SVC_OK &&
          strcmp(out, "\\\",\\\"admin\\\":true") == 0, "injection neutralized");

    CHECK(svc_json_escape("tab\tnl\n", out, sizeof(out)) == SVC_OK &&
          strcmp(out, "tab\\tnl\\n") == 0, "control chars escaped");

    char tiny[3];
    CHECK(svc_json_escape("\"x", tiny, sizeof(tiny)) == SVC_ERR_OUT_OF_RANGE &&
          tiny[0] == '\0', "overflow -> error + empty (no truncated garbage)");

    CHECK(svc_json_escape(NULL, out, sizeof(out)) == SVC_OK &&
          out[0] == '\0', "NULL input -> empty string");
}

/* ---- S4: bounded escape never reads past a NON-terminated buffer ----
   Built with -fsanitize=address; an out-of-bounds read here fails the run. */
static void test_json_escape_bounded(void)
{
    printf("[json escape bounded / OOB]\n");
    char out[64];

    /* Heap buffer of exactly 4 bytes, NOT NUL-terminated. */
    char *p = (char *)malloc(4);
    memcpy(p, "ABCD", 4);
    svc_err_t rc = svc_json_escape_n(p, 4, out, sizeof(out));
    CHECK(rc == SVC_OK && strcmp(out, "ABCD") == 0,
          "escape_n reads exactly src_len bytes (no OOB on unterminated buf)");
    free(p);

    /* Embedded NUL terminates early without scanning the rest. */
    char emb[6] = { 'a','b','\0','c','d','e' };
    CHECK(svc_json_escape_n(emb, sizeof(emb), out, sizeof(out)) == SVC_OK &&
          strcmp(out, "ab") == 0, "embedded NUL ends the logical string");

    /* Mirrors the webui call pattern: strnlen over a full, unterminated array. */
    char arr[4]; memset(arr, 'Z', sizeof(arr));   /* "ZZZZ", no NUL */
    size_t n = strnlen(arr, sizeof(arr));
    CHECK(n == 4 && svc_json_escape_n(arr, n, out, sizeof(out)) == SVC_OK &&
          strcmp(out, "ZZZZ") == 0, "strnlen+escape_n on full fixed array is safe");
}

/* ---- S3: config sanitizer (real svc_config_sanitize) ---- */
static void test_config_sanitize(void)
{
    printf("[config sanitize]\n");
    svc_config_t cfg;
    svc_config_defaults(&cfg);

    /* Inject a CRC-valid-but-unsafe configuration. */
    cfg.provisioned        = 5;          /* non-boolean */
    cfg.webui_require_auth  = 0;          /* attempt to disable auth */
    strncpy(cfg.setup_password, "secretpw", sizeof(cfg.setup_password) - 1);
    cfg.presence_slave     = 0;           /* invalid modbus addr */
    cfg.din_debounce_ms    = 60000;       /* far over max */
    cfg.presence_poll_ms   = 1;           /* under min */
    cfg.rule[0].enabled    = 9;           /* non-boolean */
    cfg.rule[0].target_relay = 200;       /* out of range */
    /* device_name fully filled, NOT NUL-terminated. */
    memset(cfg.device_name, 'A', sizeof(cfg.device_name));

    svc_config_sanitize(&cfg);

    CHECK(cfg.provisioned == 1, "provisioned normalized to 1");
    CHECK(cfg.webui_require_auth == 1, "auth FORCED on when provisioned (flag ignored)");
    CHECK(cfg.device_name[sizeof(cfg.device_name) - 1] == '\0',
          "device_name forced NUL-terminated (no OOB on later reads)");
    CHECK(cfg.presence_slave >= 1 && cfg.presence_slave <= 247,
          "modbus slave forced into 1..247");
    CHECK(cfg.din_debounce_ms <= SVC_DIN_DEBOUNCE_MAX_MS, "debounce clamped to max");
    CHECK(cfg.presence_poll_ms >= SVC_PRESENCE_POLL_MIN_MS, "poll clamped to min");
    CHECK(cfg.rule[0].enabled == 0, "rule with out-of-range relay disabled");

    /* provisioned flag with empty password must collapse to un-provisioned. */
    svc_config_t c2;
    svc_config_defaults(&c2);
    c2.provisioned = 1;
    c2.setup_password[0] = '\0';
    svc_config_sanitize(&c2);
    CHECK(c2.provisioned == 0, "provisioned+empty-password => un-provisioned");
}

/* ---- S1/S2/S6: REAL Web UI gate predicates (the functions webui.c calls) ----
   These link against components/webui/webui_authz.c, so a regression in the
   shipped gate logic fails the build here rather than slipping through. */
static void test_webui_authz(void)
{
    printf("[webui authz (real functions)]\n");

    /* Provisioning gate — the previously-exploitable ambient-LAN window is gone. */
    CHECK(webui_provisioning_allowed(false, false, NET_DOWN) == false,
          "factory LAN window does not permit provisioning");
    CHECK(webui_provisioning_allowed(false, false, NET_CONNECTING) == false,
          "ordinary LAN connectivity does not permit provisioning");
    CHECK(webui_provisioning_allowed(false, true, NET_DOWN) == true,
          "config button at boot permits provisioning");
    CHECK(webui_provisioning_allowed(false, false, NET_AP_PROVISIONING) == true,
          "AP provisioning mode permits provisioning");
    CHECK(webui_provisioning_allowed(true, true, NET_AP_PROVISIONING) == false,
          "provisioning never permitted once provisioned");

    /* /api/io GET now requires auth. */
    CHECK(webui_io_get_allowed(true, false) == false,
          "/api/io GET requires auth");
    CHECK(webui_io_get_allowed(false, false) == false,
          "/api/io GET blocked before provisioning");
    CHECK(webui_io_get_allowed(true, true) == true,
          "/api/io GET allowed for an authenticated client");

    /* Mutating gate (relay POST / OTA). */
    CHECK(webui_mutating_allowed(true, false, true) == false,
          "/api/io POST requires auth");
    CHECK(webui_mutating_allowed(true, true, false) == false,
          "/api/io POST requires CSRF");
    CHECK(webui_mutating_allowed(false, true, true) == false,
          "/api/io POST blocked before provisioning");
    CHECK(webui_mutating_allowed(true, true, true) == true,
          "/api/io POST allowed only with provisioned+auth+CSRF");
}

/* ---- debounce integrator (shipped header) ---- */
static void test_debounce(void)
{
    printf("[debounce]\n");
    dinput_debounce_t d;
    dinput_debounce_init(&d, 3, false);
    int flips = 0;
    bool seq[] = {1,0,1,1,1,1,0,0,0,0};
    for (int i = 0; i < 10; ++i) {
        if (dinput_debounce_update(&d, seq[i])) flips++;
    }
    CHECK(flips == 2, "noisy edge debounced to exactly 2 stable flips");
    CHECK(d.stable == false, "settles low after sustained low");
}

/* ---- Modbus RTU CRC reference vector ---- */
static uint16_t crc16(const uint8_t *b, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        c ^= b[i];
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    return c;
}
static void test_crc(void)
{
    printf("[modbus crc]\n");
    uint8_t f[] = {0x01,0x04,0x00,0x00,0x00,0x01};
    CHECK(crc16(f, sizeof(f)) == 0xCA31, "CRC16/MODBUS vector == 0xCA31");
}

/* ---- P6: Modbus response-length rules (mirror of modbus_master.c) ---- */
static bool read_len_valid(size_t rlen, uint16_t count) { return rlen == 5u + count*2u; }
static bool exc_len_valid(size_t rlen)                  { return rlen == 5; }
static bool write_echo_len_valid(size_t rlen)           { return rlen == 8; }
static void test_modbus_lengths(void)
{
    printf("[modbus length validation]\n");
    CHECK(read_len_valid(7, 1),  "read 1 reg -> 7 bytes accepted");
    CHECK(!read_len_valid(6, 1), "short read frame rejected");
    CHECK(!read_len_valid(9, 1), "over-long read frame rejected");
    CHECK(exc_len_valid(5),      "exception frame 5 bytes accepted");
    CHECK(!exc_len_valid(4),     "4-byte frame rejected (below minimum)");
    CHECK(write_echo_len_valid(8),  "write echo 8 bytes accepted");
    CHECK(!write_echo_len_valid(7), "short write echo rejected");
}

/* ---- P8: config migration version/size decision (mirror of storage.c) ---- */
typedef enum { USE_DIRECT, MIGRATE_V1, USE_DEFAULTS, REFUSE_NEWER } cfg_decision_t;
static cfg_decision_t cfg_decide(uint16_t ver, size_t len, size_t v1_sz, size_t v2_sz,
                                 uint16_t cur)
{
    if (ver > cur) return REFUSE_NEWER;
    if (ver == cur && len == v2_sz) return USE_DIRECT;
    if (ver == 1 && len == v1_sz)   return MIGRATE_V1;
    return USE_DEFAULTS;
}
static void test_cfg_migration(void)
{
    printf("[config migration]\n");
    const size_t V1 = 90, V2 = 160, CUR = 2;
    CHECK(cfg_decide(2, V2, V1, V2, CUR) == USE_DIRECT,  "current v2 accepted");
    CHECK(cfg_decide(1, V1, V1, V2, CUR) == MIGRATE_V1,  "v1 blob migrated");
    CHECK(cfg_decide(1, 88, V1, V2, CUR) == USE_DEFAULTS,"v1 wrong size -> defaults");
    CHECK(cfg_decide(3, V2, V1, V2, CUR) == REFUSE_NEWER,"newer schema refused");
    CHECK(cfg_decide(2, 12, V1, V2, CUR) == USE_DEFAULTS,"corrupt size -> defaults");
}

int main(void)
{
    printf("=== SVC-100 host tests ===\n");
    test_json_escape();
    test_json_escape_bounded();
    test_debounce();
    test_crc();
    test_modbus_lengths();
    test_cfg_migration();
    test_config_sanitize();
    test_webui_authz();
    printf("=== %s ===\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
