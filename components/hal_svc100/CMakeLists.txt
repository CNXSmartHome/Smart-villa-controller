# SVC-100 HAL target. Compiled only for the SVC-100 build (depends on board.h).
set(srcs "")
if(CONFIG_SVC_TARGET_SVC100_REVA)
    list(APPEND srcs "hal_svc100.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES svc_common svc_hal board
    PRIV_REQUIRES driver
)
