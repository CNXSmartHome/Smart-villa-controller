/**
 * @file svc_version.h
 * @brief Firmware semantic version. Build metadata (git hash, date) comes from
 *        the IDF app descriptor (esp_app_get_description()).
 */
#ifndef SVC_VERSION_H
#define SVC_VERSION_H

#define SVC_FW_VERSION_MAJOR 0
#define SVC_FW_VERSION_MINOR 1
#define SVC_FW_VERSION_PATCH 0
#define SVC_FW_VERSION       "0.1.0"
#define SVC_HW_REVISION      "V1"
#define SVC_PRODUCT_NAME     "Smart Villa Controller SVC-100"

#endif /* SVC_VERSION_H */
