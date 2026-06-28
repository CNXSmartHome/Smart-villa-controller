/* Host-test shim for esp_log.h — logging compiles to no-ops under test. */
#ifndef ESP_LOG_H
#define ESP_LOG_H
#define ESP_LOGE(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGD(tag, ...) ((void)0)
#define ESP_LOGV(tag, ...) ((void)0)
#endif
