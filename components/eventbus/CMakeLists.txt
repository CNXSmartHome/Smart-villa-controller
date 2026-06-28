idf_component_register(
    SRCS "eventbus.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common
    PRIV_REQUIRES freertos
)
