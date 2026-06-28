idf_component_register(
    SRCS "svc_common.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer
)
