idf_component_register(
    SRCS "storage.c" "svc_config.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board
    PRIV_REQUIRES nvs_flash esp_rom
)
