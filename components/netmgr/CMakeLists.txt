idf_component_register(
    SRCS "netmgr.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common storage eventbus
    PRIV_REQUIRES esp_wifi esp_netif esp_event
)
