idf_component_register(
    SRCS "presence.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common modbus_master eventbus health
    PRIV_REQUIRES freertos
)
