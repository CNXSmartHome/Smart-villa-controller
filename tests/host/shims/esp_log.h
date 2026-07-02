/* Host-test shim for esp_log.h — logging compiles to no-ops under test.
 *
 * The macros still "consume" the tag argument so a file-scope `static const
 * char *TAG` is considered used. Without this, host builds warn about an unused
 * TAG (on-target ESP_LOGx references it, so the warning is host-only). */
#ifndef ESP_LOG_H
#define ESP_LOG_H
#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGI(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGD(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, ...) do { (void)(tag); } while (0)
#endif
