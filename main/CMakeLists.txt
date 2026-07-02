idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        svc_common board eventbus storage
        indicator relay dinput button
        rs485 modbus_master presence
        control netmgr webui health
        svc_hal hal_svc100
    PRIV_REQUIRES freertos esp_app_format app_update
)
