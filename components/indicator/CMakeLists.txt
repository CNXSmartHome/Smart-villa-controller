idf_component_register(
    SRCS "indicator.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board
    PRIV_REQUIRES driver freertos led_strip
)
