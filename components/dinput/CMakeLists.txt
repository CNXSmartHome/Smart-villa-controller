idf_component_register(
    SRCS "dinput.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board eventbus
    PRIV_REQUIRES esp_driver_gpio freertos
)
