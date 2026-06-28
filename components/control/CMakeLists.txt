idf_component_register(
    SRCS "control.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common storage eventbus relay indicator presence dinput netmgr health
    PRIV_REQUIRES freertos esp_system esp_timer
)
