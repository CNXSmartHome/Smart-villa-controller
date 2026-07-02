idf_component_register(
    SRCS "rs485.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board
    PRIV_REQUIRES driver driver freertos
)
