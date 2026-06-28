idf_component_register(
    SRCS "indicator.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board
    PRIV_REQUIRES esp_driver_ledc freertos led_strip
)
