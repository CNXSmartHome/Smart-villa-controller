idf_component_register(
    SRCS "relay.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common board
    PRIV_REQUIRES driver freertos
)
