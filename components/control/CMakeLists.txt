idf_component_register(
    SRCS "control.c" "control_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common storage eventbus relay indicator presence dinput netmgr health logic
    PRIV_REQUIRES freertos esp_system esp_timer
)
