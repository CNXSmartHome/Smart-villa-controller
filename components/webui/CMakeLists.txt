idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common storage relay dinput presence
    PRIV_REQUIRES esp_http_server esp_app_format app_update esp_https_ota
)
