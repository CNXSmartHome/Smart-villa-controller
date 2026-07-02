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
/* strnlen() is POSIX.1-2008, not strict ISO C11. Request it before <string.h>
   so a -std=c11 host build declares it (on-target ESP-IDF already provides it). */
#define _POSIX_C_SOURCE 200809L

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
#include "logic.h"                  /* real IF/FOR/THEN logic engine */
#include "control_logic.h"          /* real config->logic adapter + fallback */
#include "hal_board.h"              /* real HAL dispatcher */
#include "kincony_profiles.h"       /* KinCony profiles + PCF8574 io map */
#include "presence_fusion.h"        /* real 2-sensor presence fusion */
#include "webui_settings.h"         /* real settings-write whitelist/apply */

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
    for (int i = 0; i < 4; ++i) {
        cfg.relay_safe_on[i] = 1;          /* unsafe energized fail-safe */
    }
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
    bool all_safe_off = true;
    for (int i = 0; i < 4; ++i) {
        all_safe_off = all_safe_off && (cfg.relay_safe_on[i] == 0);
    }
    CHECK(all_safe_off, "relay safe state forced de-energized");

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

/* ---- SVC-013: Logic engine v0.1 (real logic.c, IF/FOR/THEN) ---- */
static void test_logic_engine(void)
{
    printf("[logic engine v0.1]\n");
    logic_engine_t e;
    logic_engine_init(&e);

    /* Rule 0: IF presence ACTIVE FOR 100 ms THEN relay 0 ON, 2 s off-delay. */
    logic_ruleset_t rs = {0};
    rs.count = 1;
    rs.rule[0] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_PRESENCE,
                                 .cond = LOGIC_COND_ACTIVE, .for_ms = 100,
                                 .action = LOGIC_ACT_ON, .target_relay = 0,
                                 .off_delay_s = 2 };
    CHECK(logic_ruleset_validate(&rs, 8, 4) == SVC_OK, "ruleset validates");

    uint32_t mask = 0xFFFFFFFFu;
    logic_input_t present = { .presence = LOGIC_PRESENCE_PRESENT, .din_mask = 0 };
    logic_input_t absent  = { .presence = LOGIC_PRESENCE_ABSENT,  .din_mask = 0 };

    /* t=1000: condition just became true -> FOR not yet satisfied. */
    logic_engine_eval(&e, &rs, &present, 1000, 4, &mask);
    CHECK(mask == 0x00, "FOR not yet met -> relay stays OFF");

    /* t=1100: held 100 ms -> qualifies -> relay 0 ON. */
    logic_engine_eval(&e, &rs, &present, 1100, 4, &mask);
    CHECK(mask == 0x01, "condition held FOR 100ms -> relay 0 ON");

    /* t=1200: presence drops -> off-delay linger keeps relay ON. */
    logic_engine_eval(&e, &rs, &absent, 1200, 4, &mask);
    CHECK(mask == 0x01, "off-delay linger keeps relay ON after presence drop");

    /* t=3300: >2 s after drop -> linger expired -> relay OFF. */
    logic_engine_eval(&e, &rs, &absent, 3300, 4, &mask);
    CHECK(mask == 0x00, "relay OFF after off-delay expires");

    /* FOR debounce: a short blip < for_ms must never turn the relay on. */
    logic_engine_init(&e);
    logic_engine_eval(&e, &rs, &present, 5000, 4, &mask);  /* start */
    logic_engine_eval(&e, &rs, &absent,  5050, 4, &mask);  /* drop at 50ms */
    CHECK(mask == 0x00, "blip shorter than FOR never energizes");

    /* Safety interlock: a qualified OFF rule beats a qualified ON rule. */
    logic_engine_t e2;
    logic_engine_init(&e2);
    logic_ruleset_t rs2 = {0};
    rs2.count = 2;
    rs2.rule[0] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_PRESENCE,
                                  .cond = LOGIC_COND_ACTIVE, .for_ms = 0,
                                  .action = LOGIC_ACT_ON, .target_relay = 1 };
    rs2.rule[1] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_DININPUT,
                                  .channel = 3, .cond = LOGIC_COND_ACTIVE,
                                  .for_ms = 0, .action = LOGIC_ACT_OFF,
                                  .target_relay = 1 };
    logic_ruleset_validate(&rs2, 8, 4);
    logic_input_t both = { .presence = LOGIC_PRESENCE_PRESENT,
                           .din_mask = (1u << 3) };
    logic_engine_eval(&e2, &rs2, &both, 10000, 4, &mask);
    CHECK((mask & 0x02) == 0, "OFF interlock beats ON request (de-energize wins)");

    /* Digital-input INACTIVE condition + now==0 edge (no false qualify). */
    logic_engine_t e3;
    logic_engine_init(&e3);
    logic_ruleset_t rs3 = {0};
    rs3.count = 1;
    rs3.rule[0] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_DININPUT,
                                  .channel = 0, .cond = LOGIC_COND_ACTIVE,
                                  .for_ms = 50, .action = LOGIC_ACT_ON,
                                  .target_relay = 2 };
    logic_input_t din0 = { .presence = 0, .din_mask = 0x01 };
    logic_engine_eval(&e3, &rs3, &din0, 0, 4, &mask);
    CHECK((mask & 0x04) == 0, "now==0 start does not falsely qualify (FOR honored)");

    /* Validation disables out-of-range / reserved-action rules. */
    logic_ruleset_t rs4 = {0};
    rs4.count = 2;
    rs4.rule[0] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_DININPUT,
                                  .channel = 99, .action = LOGIC_ACT_ON,
                                  .target_relay = 0 };
    rs4.rule[1] = (logic_rule_t){ .enabled = 1, .src = LOGIC_SRC_PRESENCE,
                                  .action = LOGIC_ACT_TOGGLE, .target_relay = 9 };
    logic_ruleset_validate(&rs4, 8, 4);
    CHECK(rs4.rule[0].enabled == 0, "rule with bad DI channel disabled");
    CHECK(rs4.rule[1].enabled == 0, "reserved TOGGLE / bad relay disabled");
}

/* ---- SVC-013 regression: safety gaps found in review (Codex) ----
   Built with ASan/UBSan so the unvalidated out-of-range channel test would trap
   on any undefined shift. */
static void test_logic_engine_safety(void)
{
    printf("[logic engine safety/regression]\n");
    uint32_t mask = 0xFFFFFFFFu;

    /* (A) OFF interlock must CANCEL a stale ON linger, so the relay cannot
           re-energize after the interlock clears but before linger expiry. */
    logic_engine_t eA; logic_engine_init(&eA);
    logic_ruleset_t a = {0}; a.count = 2;
    a.rule[0] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_PRESENCE,.cond=LOGIC_COND_ACTIVE,
                                .for_ms=0,.action=LOGIC_ACT_ON,.target_relay=1,.off_delay_s=10 };
    a.rule[1] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_DININPUT,.channel=3,.cond=LOGIC_COND_ACTIVE,
                                .for_ms=0,.action=LOGIC_ACT_OFF,.target_relay=1 };
    logic_ruleset_validate(&a, 8, 4);
    logic_input_t p_no  = { .presence=LOGIC_PRESENCE_PRESENT, .din_mask=0 };
    logic_input_t a_no  = { .presence=LOGIC_PRESENCE_ABSENT,  .din_mask=0 };
    logic_input_t a_emg = { .presence=LOGIC_PRESENCE_ABSENT,  .din_mask=(1u<<3) };
    logic_engine_eval(&eA,&a,&p_no,  0,   4,&mask); CHECK((mask&2)==2,"relay1 ON when present");
    logic_engine_eval(&eA,&a,&a_no,  1000,4,&mask); CHECK((mask&2)==2,"linger holds relay1 ON after presence drop");
    logic_engine_eval(&eA,&a,&a_emg, 1500,4,&mask); CHECK((mask&2)==0,"emergency OFF de-energizes relay1");
    logic_engine_eval(&eA,&a,&a_no,  2000,4,&mask); CHECK((mask&2)==0,"OFF cancelled linger: no re-energize after interlock clears");

    /* (B) Unvalidated, out-of-range runtime rules must fail closed (no UB). */
    logic_engine_t eB; logic_engine_init(&eB);
    logic_ruleset_t b = {0}; b.count = 2;
    b.rule[0] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_DININPUT,.channel=99,
                                .cond=LOGIC_COND_ACTIVE,.for_ms=0,.action=LOGIC_ACT_ON,.target_relay=0 };
    b.rule[1] = (logic_rule_t){ .enabled=1,.src=7 /*invalid*/,.for_ms=0,
                                .action=LOGIC_ACT_ON,.target_relay=1 };
    b.rule[2] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_PRESENCE,.cond=99,
                                .for_ms=0,.action=LOGIC_ACT_ON,.target_relay=2 };
    b.count = 3;
    /* deliberately NOT validated */
    logic_input_t allon = { .presence=LOGIC_PRESENCE_ABSENT, .din_mask=0xFF };
    logic_engine_eval(&eB,&b,&allon, 5000,4,&mask);
    CHECK(mask==0, "unvalidated out-of-range/invalid rules fail closed (no relay, no UB)");

    /* (C) Millisecond wrap-around: FOR dwell across the uint32 boundary. */
    logic_engine_t eC; logic_engine_init(&eC);
    logic_ruleset_t c = {0}; c.count = 1;
    c.rule[0] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_PRESENCE,.cond=LOGIC_COND_ACTIVE,
                                .for_ms=100,.action=LOGIC_ACT_ON,.target_relay=0,.off_delay_s=0 };
    logic_ruleset_validate(&c,8,4);
    uint32_t t1 = 0xFFFFFFF0u;             /* just before wrap */
    uint32_t t2 = (uint32_t)(t1 + 120u);    /* +120ms, wraps past 0 */
    logic_engine_eval(&eC,&c,&p_no, t1,4,&mask); CHECK((mask&1)==0,"pre-wrap: FOR not yet met");
    logic_engine_eval(&eC,&c,&p_no, t2,4,&mask); CHECK((mask&1)==1,"dwell across uint32 wrap still qualifies");

    /* (C2) OFF-delay linger expiry across the wrap boundary. */
    logic_engine_t eD; logic_engine_init(&eD);
    logic_ruleset_t d = {0}; d.count = 1;
    d.rule[0] = (logic_rule_t){ .enabled=1,.src=LOGIC_SRC_PRESENCE,.cond=LOGIC_COND_ACTIVE,
                                .for_ms=0,.action=LOGIC_ACT_ON,.target_relay=0,.off_delay_s=1 };
    logic_ruleset_validate(&d,8,4);
    uint32_t w = 0xFFFFFE00u;
    logic_engine_eval(&eD,&d,&p_no, w,             4,&mask); CHECK((mask&1)==1,"present -> ON (wrap base)");
    logic_engine_eval(&eD,&d,&a_no, (uint32_t)(w+300), 4,&mask); CHECK((mask&1)==1,"linger holds before wrap");
    logic_engine_eval(&eD,&d,&a_no, (uint32_t)(w+1400),4,&mask); CHECK((mask&1)==0,"linger expires correctly after wrap");
}

/* ---- v3 adapter + SVC-012 dry-contact fallback (real control_logic.c) ---- */
static void test_control_logic_adapter(void)
{
    printf("[config->logic adapter + fallback]\n");

    /* Adapter: presence rule, on_active=1, action ON. */
    svc_rule_t sr = { .enabled=1, .trigger_src=0, .trigger_chan=0, .target_relay=2,
                      .on_active=1, .off_delay_s=30, .for_ms=500,
                      .action=SVC_RULE_ACTION_ON };
    logic_rule_t lr;
    svc_rule_to_logic(&sr, &lr);
    CHECK(lr.src==LOGIC_SRC_PRESENCE && lr.cond==LOGIC_COND_ACTIVE &&
          lr.action==LOGIC_ACT_ON && lr.for_ms==500 && lr.off_delay_s==30 &&
          lr.target_relay==2, "presence/on_active=1/ON maps correctly");

    /* Adapter: digital-input rule, on_active=0 -> INACTIVE, action OFF. */
    svc_rule_t sr2 = { .enabled=1, .trigger_src=1, .trigger_chan=5, .target_relay=1,
                       .on_active=0, .off_delay_s=0, .for_ms=0,
                       .action=SVC_RULE_ACTION_OFF };
    svc_rule_to_logic(&sr2, &lr);
    CHECK(lr.src==LOGIC_SRC_DININPUT && lr.channel==5 &&
          lr.cond==LOGIC_COND_INACTIVE && lr.action==LOGIC_ACT_OFF,
          "dinput/on_active=0/OFF maps correctly");

    /* Ruleset from config validates out-of-range rules. */
    svc_config_t cfg;
    svc_config_defaults(&cfg);
    cfg.rule[1] = (svc_rule_t){ .enabled=1, .trigger_src=1, .trigger_chan=99,
                                .target_relay=0, .on_active=1 };  /* bad channel */
    logic_ruleset_t rs;
    logic_ruleset_from_config(&cfg, 8, 4, &rs);
    CHECK(rs.count == (SVC_RULE_MAX < LOGIC_RULE_MAX ? SVC_RULE_MAX : LOGIC_RULE_MAX),
          "ruleset_from_config maps all rule slots");
    CHECK(rs.rule[1].enabled == 0, "out-of-range channel rule disabled by validate");

    /* SVC-012 dry-contact fallback. */
    CHECK(presence_effective(LOGIC_PRESENCE_PRESENT, false, 0x00, true, 3)
          == LOGIC_PRESENCE_PRESENT, "fresh RS485 is authoritative (PRESENT)");
    CHECK(presence_effective(LOGIC_PRESENCE_ABSENT, false, 0xFF, true, 3)
          == LOGIC_PRESENCE_ABSENT, "fresh RS485 is authoritative (ABSENT)");
    CHECK(presence_effective(LOGIC_PRESENCE_UNKNOWN, true, (1u<<3), true, 3)
          == LOGIC_PRESENCE_PRESENT, "stale + fallback DI asserted -> PRESENT");
    CHECK(presence_effective(LOGIC_PRESENCE_UNKNOWN, true, 0x00, true, 3)
          == LOGIC_PRESENCE_ABSENT, "stale + fallback DI clear -> ABSENT");
    CHECK(presence_effective(LOGIC_PRESENCE_PRESENT, true, 0xFF, false, 3)
          == LOGIC_PRESENCE_UNKNOWN, "stale + no fallback -> UNKNOWN");
    CHECK(presence_effective(LOGIC_PRESENCE_PRESENT, true, 0xFF, true, 99)
          == LOGIC_PRESENCE_UNKNOWN, "stale + bad fallback channel -> UNKNOWN (safe)");
}

/* ===================== SVC-CORE-001 — HAL foundation tests ===================== */

/* Mock HAL target: 16 relays / 16 inputs, used to host-test the dispatcher. */
static bool     g_mock_relay[HAL_MAX_RELAY];
static uint32_t g_mock_din;
static bool     g_mock_safe_fault;   /* simulate PCF8574 I2C fault on safe-state */

static svc_err_t mk_init(void){ return SVC_OK; }
static svc_err_t mk_rw(uint8_t i,bool on){ if(i>=HAL_MAX_RELAY)return SVC_ERR_OUT_OF_RANGE; g_mock_relay[i]=on; return SVC_OK; }
static svc_err_t mk_rwm(uint32_t m){ for(int i=0;i<HAL_MAX_RELAY;i++) g_mock_relay[i]=(m>>i)&1u; return SVC_OK; }
static svc_err_t mk_rr(uint8_t i,bool*on){ if(i>=HAL_MAX_RELAY)return SVC_ERR_OUT_OF_RANGE; *on=g_mock_relay[i]; return SVC_OK; }
static uint32_t  mk_dim(void){ return g_mock_din; }
static svc_err_t mk_safe(void){
    if(g_mock_safe_fault) return SVC_ERR_BUS_TIMEOUT;
    for(int i=0;i<HAL_MAX_RELAY;i++) g_mock_relay[i]=false;
    return SVC_OK;
}
static svc_err_t mk_rs485(uint8_t p,hal_rs485_config_t*c){ if(p>=2)return SVC_ERR_OUT_OF_RANGE; c->uart_port=p; return SVC_OK; }
static const hal_driver_t MOCK_DRV = { mk_init, mk_rw, mk_rwm, mk_rr, NULL, mk_dim, mk_safe, mk_rs485 };
static const board_profile_t MOCK_PROFILE = {
    .board_id="mock16", .board_name="Mock16", .relay_count=16, .din_count=16,
    .rs485_count=2, .has_presence_modbus=false, .has_physical_provision_button=false,
    .relay_labels=NULL, .din_labels=NULL, .relay_polarity=RELAY_ACTIVE_HIGH,
    .safe_state=SAFE_STATE_DEENERGIZED,
    .health={ .require_presence_modbus=false, .allow_presence_degraded=true,
              .require_relay_safe=true, .boot_window_ms=30000 }, .caps=0 };

static void test_hal_dispatcher(void)
{
    printf("[hal dispatcher + 16/32 mask + OOR]\n");
    memset(g_mock_relay,0,sizeof(g_mock_relay)); g_mock_safe_fault=false;
    CHECK(hal_board_register(&MOCK_DRV,&MOCK_PROFILE)==SVC_OK, "register valid driver+profile");
    CHECK(hal_relay_count()==16 && hal_din_count()==16, "counts from profile");

    bool b=false;
    CHECK(hal_relay_set(15,true)==SVC_OK && hal_relay_get(15,&b)==SVC_OK && b,
          "relay 15 set/get (>8, 32-bit capable)");
    CHECK(hal_relay_set(16,true)==SVC_ERR_OUT_OF_RANGE, "relay index >= count rejected (fail closed)");
    CHECK(hal_relay_get(99,&b)==SVC_ERR_OUT_OF_RANGE, "relay get OOR rejected");

    CHECK(hal_relay_set_mask(0xFFFFu)==SVC_OK && hal_relay_get_mask()==0xFFFFu,
          "16-relay mask applied");
    CHECK(hal_relay_set_mask(0xFFFFFFFFu)==SVC_OK && hal_relay_get_mask()==0xFFFFu,
          "bits beyond relay_count dropped (fail closed)");
    CHECK(hal_relay_set_mask(0x8000u)==SVC_OK && hal_relay_get_mask()==0x8000u,
          "high relay bit (15) addressable");

    CHECK(hal_board_apply_safe_state()==SVC_OK && hal_relay_get_mask()==0,
          "apply_safe_state de-energizes all");

    /* register must reject a profile exceeding HAL maxima. */
    board_profile_t bad = MOCK_PROFILE; bad.relay_count = 33;
    CHECK(hal_board_register(&MOCK_DRV,&bad)!=SVC_OK, "profile relay_count>HAL_MAX rejected");
    /* restore */
    hal_board_register(&MOCK_DRV,&MOCK_PROFILE);
}

static void test_hal_failsafe(void)
{
    printf("[hal PCF8574 fault -> fail-safe]\n");
    hal_board_register(&MOCK_DRV,&MOCK_PROFILE);
    g_mock_safe_fault = true;   /* simulate I2C bus fault on the expander */
    CHECK(hal_board_apply_safe_state()==SVC_ERR_BUS_TIMEOUT,
          "I2C fault on safe-state returns fail-safe error (caller faults)");
    g_mock_safe_fault = false;
}

static void test_config_board_id(void)
{
    printf("[config board_id guard]\n");
    svc_config_t cfg; svc_config_defaults(&cfg);
    CHECK(svc_config_board_matches(&cfg,"kincony_kc868_a8")==true,
          "empty/unbranded board_id matches (caller stamps it)");
    svc_config_set_board_id(&cfg,"svc100_reva");
    CHECK(svc_config_board_matches(&cfg,"svc100_reva")==true, "matching board_id accepted");
    CHECK(svc_config_board_matches(&cfg,"kincony_kc868_a8")==false,
          "WRONG board_id rejected (will factory-default)");
}

static void test_logic_32relay(void)
{
    printf("[logic 32-bit relay mask]\n");
    logic_engine_t e; logic_engine_init(&e);
    logic_ruleset_t rs={0}; rs.count=1;
    rs.rule[0]=(logic_rule_t){ .enabled=1,.src=LOGIC_SRC_PRESENCE,.cond=LOGIC_COND_ACTIVE,
                               .for_ms=0,.action=LOGIC_ACT_ON,.target_relay=15 };
    logic_ruleset_validate(&rs,16,16);
    logic_input_t in={ .presence=LOGIC_PRESENCE_PRESENT, .din_mask=0 };
    uint32_t mask=0;
    logic_engine_eval(&e,&rs,&in,1000,16,&mask);
    CHECK(mask==(1u<<15), "logic engine drives relay 15 via 32-bit mask");
}

static void test_health_profile(void)
{
    printf("[OTA health profile-driven]\n");
    const board_health_policy_t svc100 = { .require_presence_modbus=true,
        .allow_presence_degraded=false, .require_relay_safe=true, .boot_window_ms=30000 };
    /* KinCony: presence not required, degraded allowed. */
    CHECK(board_health_ota_ok(&KINCONY_KC868_A8.health, true,/*presence*/false,
                              true,true,true,/*fault*/false)==true,
          "KinCony OTA OK with NO presence (degraded allowed)");
    /* SVC-100: presence required. */
    CHECK(board_health_ota_ok(&svc100, true,false,true,true,true,false)==false,
          "SVC-100 OTA BLOCKED without presence");
    CHECK(board_health_ota_ok(&svc100, true,true,true,true,true,false)==true,
          "SVC-100 OTA OK once presence ran");
    /* Fault and missing relay-safe always block. */
    CHECK(board_health_ota_ok(&KINCONY_KC868_A8.health, true,true,true,true,true,true)==false,
          "latched fault blocks OTA");
    CHECK(board_health_ota_ok(&KINCONY_KC868_A8.health, true,true,/*relay_safe*/false,true,true,false)==false,
          "missing relay-safe blocks OTA");
}

static void test_kincony_pcf8574(void)
{
    printf("[KinCony PCF8574 mapping]\n");
    const kincony_io_map_t *a8 = &KINCONY_KC868_A8_IO;
    CHECK(kincony_relay_safe_byte(a8)==0xFF, "active-low safe byte = 0xFF (relays OFF)");

    uint8_t out[KINCONY_MAX_CHIPS]={0};
    kincony_pack_relays(a8, 0x01, out);   /* energize relay 0 */
    CHECK(out[0]==0xFE, "active-low: energizing relay0 writes 0 (0xFE)");
    kincony_pack_relays(a8, 0x00, out);
    CHECK(out[0]==0xFF, "no relays energized -> idle byte 0xFF (safe)");

    uint8_t chip,bit;
    CHECK(kincony_relay_locate(a8,7,&chip,&bit) && chip==0 && bit==7, "relay 7 -> chip0 bit7");
    /* A16: relay 8 lands on chip 1 bit 0 */
    const kincony_io_map_t *a16 = &KINCONY_KC868_A16_IO;
    CHECK(kincony_relay_locate(a16,8,&chip,&bit) && chip==1 && bit==0, "A16 relay 8 -> chip1 bit0");
    uint8_t o2[KINCONY_MAX_CHIPS]={0};
    kincony_pack_relays(a16, (1u<<8), o2);
    CHECK(o2[0]==0xFF && o2[1]==0xFE, "A16 relay8 energizes chip1 only");

    /* active-low input: closed contact reads 0 -> asserted. */
    uint8_t in[KINCONY_MAX_CHIPS]={0xFE,0};   /* chip0 bit0 low */
    CHECK(kincony_unpack_inputs(a8,in)==0x01, "active-low input bit0 asserted");
}

/* ===================== 2-sensor presence fusion ===================== */
static void test_presence_fusion(void)
{
    printf("[presence fusion (2 sensors)]\n");
    presence_fusion_t f; presence_fusion_init(&f);
    const uint32_t D = 1000;   /* room_empty_delay = 1 s */
    pf_sensor_in_t in[2];

    /* OR rule: any PRESENT -> occupied. */
    in[0]=(pf_sensor_in_t){1,PF_PRESENT}; in[1]=(pf_sensor_in_t){1,PF_ABSENT};
    CHECK(presence_fusion_eval(&f,in,2,PF_FAULT_HOLD,D,0)==PF_PRESENT,
          "sensor1 present OR sensor2 absent -> occupied");

    /* Both empty: hold during delay, then empty. */
    in[0]=(pf_sensor_in_t){1,PF_ABSENT}; in[1]=(pf_sensor_in_t){1,PF_ABSENT};
    CHECK(presence_fusion_eval(&f,in,2,PF_FAULT_HOLD,D,1000)==PF_PRESENT,
          "both empty within delay -> still occupied (no instant OFF)");
    CHECK(presence_fusion_eval(&f,in,2,PF_FAULT_HOLD,D,2000)==PF_ABSENT,
          "both empty for delay -> room empty");

    /* Re-occupy cancels the empty state immediately. */
    in[1]=(pf_sensor_in_t){1,PF_PRESENT};
    CHECK(presence_fusion_eval(&f,in,2,PF_FAULT_HOLD,D,2500)==PF_PRESENT,
          "re-occupied cancels empty");

    /* FAULT must not falsely turn off: HOLD policy, one absent + one faulted. */
    presence_fusion_t g; presence_fusion_init(&g);
    in[0]=(pf_sensor_in_t){1,PF_ABSENT}; in[1]=(pf_sensor_in_t){1,PF_UNKNOWN};
    CHECK(presence_fusion_eval(&g,in,2,PF_FAULT_HOLD,D,0)==PF_PRESENT,
          "HOLD: faulted sensor blocks empty");
    CHECK(presence_fusion_eval(&g,in,2,PF_FAULT_HOLD,D,9000)==PF_PRESENT,
          "HOLD: faulted sensor never falsely turns off (even past delay)");

    /* ASSUME_OCCUPIED: a faulted sensor counts as present. */
    presence_fusion_t h; presence_fusion_init(&h);
    in[0]=(pf_sensor_in_t){1,PF_ABSENT}; in[1]=(pf_sensor_in_t){1,PF_UNKNOWN};
    CHECK(presence_fusion_eval(&h,in,2,PF_FAULT_ASSUME_OCCUPIED,D,0)==PF_PRESENT,
          "ASSUME_OCCUPIED: faulted -> occupied");

    /* IGNORE: faulted excluded; the working sensor decides. */
    presence_fusion_t k; presence_fusion_init(&k);
    in[0]=(pf_sensor_in_t){1,PF_ABSENT}; in[1]=(pf_sensor_in_t){1,PF_UNKNOWN};
    presence_fusion_eval(&k,in,2,PF_FAULT_IGNORE,D,0);
    CHECK(presence_fusion_eval(&k,in,2,PF_FAULT_IGNORE,D,1000)==PF_ABSENT,
          "IGNORE: working sensor absent for delay -> empty");
    /* IGNORE with ALL sensors faulted -> UNKNOWN (caller failsafe). */
    presence_fusion_t m; presence_fusion_init(&m);
    in[0]=(pf_sensor_in_t){1,PF_UNKNOWN}; in[1]=(pf_sensor_in_t){1,PF_UNKNOWN};
    CHECK(presence_fusion_eval(&m,in,2,PF_FAULT_IGNORE,D,0)==PF_UNKNOWN,
          "IGNORE: all faulted -> UNKNOWN");

    /* Disabled second sensor is not counted. */
    presence_fusion_t s; presence_fusion_init(&s);
    in[0]=(pf_sensor_in_t){1,PF_PRESENT}; in[1]=(pf_sensor_in_t){0,PF_UNKNOWN};
    CHECK(presence_fusion_eval(&s,in,2,PF_FAULT_HOLD,D,0)==PF_PRESENT,
          "disabled sensor ignored; single sensor decides");
}

static void test_config_v5_presence(void)
{
    printf("[config v5 multi-sensor]\n");
    svc_config_t cfg; svc_config_defaults(&cfg);
    CHECK(cfg.presence_sensor_count==1 && cfg.presence_sensor[0].type==1,
          "default: 1 RS485 sensor");
    CHECK(cfg.room_empty_delay_sec==30 && cfg.sensor_fault_policy==0,
          "default: 30 s empty delay, HOLD fault policy");

    /* Inject invalid values; sanitize must bound/disable. */
    cfg.presence_sensor_count = 9;                 /* > max */
    cfg.presence_sensor[0].type = 1; cfg.presence_sensor[0].modbus_addr = 0; /* bad addr */
    cfg.presence_sensor[1].type = 2; cfg.presence_sensor[1].din_index = 200; /* bad chan */
    cfg.room_empty_delay_sec = 60000;              /* over max */
    cfg.sensor_fault_policy = 7;                   /* invalid */
    svc_config_sanitize(&cfg);
    CHECK(cfg.presence_sensor_count<=SVC_PRESENCE_MAX_SENSORS, "sensor count clamped");
    CHECK(cfg.presence_sensor[0].type==0, "RS485 sensor with bad modbus addr disabled");
    CHECK(cfg.presence_sensor[1].type==0, "dry-contact sensor with bad DI index disabled");
    CHECK(cfg.room_empty_delay_sec<=SVC_ROOM_EMPTY_DELAY_MAX, "empty delay clamped");
    CHECK(cfg.sensor_fault_policy<=2, "fault policy normalized to valid range");
}

/* Migration relies on distinct blob sizes to tell schema versions apart; if any
   two frozen layouts collide, config_migrate could misinterpret a blob. */
static void test_schema_sizes(void)
{
    printf("[config schema sizes distinct]\n");
    size_t s[5] = { sizeof(svc_config_v1_t), sizeof(svc_config_v2_t),
                    sizeof(svc_config_v3_t), sizeof(svc_config_v4_t),
                    sizeof(svc_config_t) /* v5 */ };
    bool distinct = true;
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j)
            if (s[i] == s[j]) distinct = false;
    CHECK(distinct, "v1..v5 struct sizes are pairwise distinct (migration-safe)");
    CHECK(sizeof(svc_config_t) > sizeof(svc_config_v4_t),
          "v5 grew over v4 (added multi-sensor block)");
}

/* ===================== SVC-014 settings write (whitelist/apply) ============= */
static void test_webui_settings(void)
{
    printf("[webui settings write (SVC-014)]\n");

    /* Whitelist: installer fields writable; secrets/identity NOT. */
    CHECK(webui_settings_is_writable("device_name"), "device_name writable");
    CHECK(webui_settings_is_writable("wifi_pass"), "wifi_pass writable (body only)");
    CHECK(webui_settings_is_writable("presence_1_type"), "presence sensor writable");
    CHECK(!webui_settings_is_writable("setup_password"),
          "setup_password NOT writable via settings (no weakening auth)");
    CHECK(!webui_settings_is_writable("board_id"), "board_id NOT writable");
    CHECK(!webui_settings_is_writable("provisioned"), "provisioned NOT writable");
    CHECK(!webui_settings_is_writable("webui_require_auth"),
          "webui_require_auth NOT writable (auth can't be disabled)");
    CHECK(!webui_settings_is_writable("version") &&
          !webui_settings_is_writable("crc"), "version/crc NOT writable");

    svc_config_t cfg; svc_config_defaults(&cfg);

    CHECK(webui_settings_apply(&cfg, "device_name", "Villa 12") == SVC_OK &&
          strcmp(cfg.device_name, "Villa 12") == 0, "apply device_name");
    CHECK(webui_settings_apply(&cfg, "room_empty_delay_sec", "45") == SVC_OK &&
          cfg.room_empty_delay_sec == 45, "apply room_empty_delay_sec");
    CHECK(webui_settings_apply(&cfg, "presence_2_type", "2") == SVC_OK &&
          cfg.presence_sensor[1].type == 2, "apply presence_2_type");

    /* Protected fields are rejected and leave cfg untouched. */
    char before[SVC_SETUP_PW_MAX]; memcpy(before, cfg.setup_password, sizeof(before));
    CHECK(webui_settings_apply(&cfg, "setup_password", "hacked") == SVC_ERR_OUT_OF_RANGE &&
          memcmp(before, cfg.setup_password, sizeof(before)) == 0,
          "setup_password write rejected, cfg unchanged");
    CHECK(webui_settings_apply(&cfg, "board_id", "evil") == SVC_ERR_OUT_OF_RANGE,
          "board_id write rejected");

    /* Out-of-range values written then sanitized -> made safe. */
    webui_settings_apply(&cfg, "presence_1_type", "1");          /* RS485 */
    webui_settings_apply(&cfg, "presence_1_modbus_addr", "0");   /* invalid addr */
    webui_settings_apply(&cfg, "sensor_fault_policy", "9");      /* invalid */
    svc_config_sanitize(&cfg);
    CHECK(cfg.presence_sensor[0].type == 0, "bad modbus addr -> sensor disabled by sanitize");
    CHECK(cfg.sensor_fault_policy <= 2, "bad fault policy normalized by sanitize");
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
    test_logic_engine();
    test_logic_engine_safety();
    test_control_logic_adapter();
    test_hal_dispatcher();
    test_hal_failsafe();
    test_config_board_id();
    test_logic_32relay();
    test_health_profile();
    test_kincony_pcf8574();
    test_presence_fusion();
    test_config_v5_presence();
    test_schema_sizes();
    test_webui_settings();
    printf("=== %s ===\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
