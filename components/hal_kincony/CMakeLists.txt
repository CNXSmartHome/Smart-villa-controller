# KinCony HAL target. The pure PCF8574 map always compiles (host-testable too);
# the I2C driver compiles only for KinCony builds.
set(srcs "kincony_io_map.c")
if(CONFIG_SVC_TARGET_KINCONY_A8 OR CONFIG_SVC_TARGET_KINCONY_A16)
    list(APPEND srcs "hal_kincony.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES svc_common svc_hal
    PRIV_REQUIRES driver
)
