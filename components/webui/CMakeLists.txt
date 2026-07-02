idf_component_register(
    SRCS "webui.c" "webui_authz.c" "webui_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common storage relay dinput presence eventbus health control netmgr svc_hal
    PRIV_REQUIRES esp_http_server esp_app_format app_update esp_https_ota esp_hw_support
)
