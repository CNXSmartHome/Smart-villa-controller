/**
 * @file netmgr.c
 * @brief Connection manager implementation (structural stub, see netmgr.h).
 *
 * The lifecycle, state tracking, and event-bus publishing are implemented. The
 * concrete radio/PHY bring-up is isolated in netmgr_wifi_bringup() /
 * netmgr_eth_bringup() and intentionally deferred; they return
 * SVC_ERR_NOT_IMPLEMENTED so callers can detect and degrade gracefully. This
 * keeps the standalone controller fully functional now and confines the
 * remaining networking work to clearly marked TODOs.
 */
#include "netmgr.h"
#include "eventbus.h"
#include "health.h"

static const char *TAG = "netmgr";

static volatile netmgr_state_t s_state = NET_DOWN;

static void publish(svc_event_type_t t, uint8_t iface)
{
    svc_event_t ev = { .type = t, .arg0 = iface };
    (void)eventbus_post(&ev, 20);
}

/* TODO(net): esp_netif + esp_event loop + esp_wifi STA bring-up. */
static svc_err_t netmgr_wifi_bringup(const svc_config_t *cfg)
{
    (void)cfg;
    ESP_LOGW(TAG, "wifi bring-up not implemented yet");
    return SVC_ERR_NOT_IMPLEMENTED;
}

/* TODO(net): esp_eth (RMII/SPI PHY) bring-up for the optional Ethernet build. */
static svc_err_t netmgr_eth_bringup(const svc_config_t *cfg)
{
    (void)cfg;
    ESP_LOGW(TAG, "eth bring-up not implemented yet");
    return SVC_ERR_NOT_IMPLEMENTED;
}

svc_err_t netmgr_start(const svc_config_t *cfg)
{
    SVC_CHECK_ARG(cfg != NULL);

    if (!cfg->wifi_enabled && !cfg->eth_enabled) {
        ESP_LOGI(TAG, "networking disabled in config; standalone mode");
        s_state = NET_DOWN;
        /* "Failed safely" — disabled-by-config is a settled, healthy outcome. */
        health_report(HEALTH_CHK_NET_SETTLED, true);
        return SVC_OK;   /* intentional: not an error */
    }

    s_state = NET_CONNECTING;
    svc_err_t rc = SVC_ERR_NOT_IMPLEMENTED;
    if (cfg->eth_enabled) {
        rc = netmgr_eth_bringup(cfg);
    }
    if (rc != SVC_OK && cfg->wifi_enabled) {
        rc = netmgr_wifi_bringup(cfg);
    }

    if (rc == SVC_OK) {
        s_state = NET_UP;
        publish(EVT_NET_UP, 0);
    } else {
        s_state = NET_DOWN;
        publish(EVT_NET_DOWN, 0);
        ESP_LOGW(TAG, "network bring-up deferred (0x%x)", (int)rc);
    }
    /* Either way the network outcome is now settled (up, or down-but-safe). */
    health_report(HEALTH_CHK_NET_SETTLED, true);
    /* Return SVC_OK regardless: optional layer must never block boot. */
    return SVC_OK;
}

svc_err_t netmgr_enter_provisioning(void)
{
    ESP_LOGW(TAG, "provisioning requested but AP mode is not implemented");
    /* TODO(net): start SoftAP + captive config portal, then publish
       NET_AP_PROVISIONING only after the LAN/STA path is no longer exposed. */
    return SVC_ERR_NOT_IMPLEMENTED;
}

netmgr_state_t netmgr_state(void) { return s_state; }
bool netmgr_is_up(void) { return s_state == NET_UP; }
