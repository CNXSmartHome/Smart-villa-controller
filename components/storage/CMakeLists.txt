idf_component_register(
    SRCS "storage.c" "svc_config.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common svc_hal
    PRIV_REQUIRES nvs_flash esp_rom
)
