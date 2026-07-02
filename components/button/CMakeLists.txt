idf_component_register(
    SRCS "button.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board eventbus
    PRIV_REQUIRES driver freertos
)
