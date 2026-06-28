idf_component_register(
    SRCS "presence.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common modbus_master eventbus
    PRIV_REQUIRES freertos
)
