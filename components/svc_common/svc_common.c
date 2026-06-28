/**
 * @file svc_common.c
 * @brief Implementation of project-wide helpers (see svc_common.h).
 */
#include "svc_common.h"
#include "esp_timer.h"

const char *svc_err_to_name(svc_err_t err)
{
    switch (err) {
    case SVC_OK:                  return "SVC_OK";
    case SVC_ERR_NOT_IMPLEMENTED: return "SVC_ERR_NOT_IMPLEMENTED";
    case SVC_ERR_BUS_TIMEOUT:     return "SVC_ERR_BUS_TIMEOUT";
    case SVC_ERR_CRC:             return "SVC_ERR_CRC";
    case SVC_ERR_MODBUS_EXC:      return "SVC_ERR_MODBUS_EXC";
    case SVC_ERR_CONFIG_VERSION:  return "SVC_ERR_CONFIG_VERSION";
    case SVC_ERR_OUT_OF_RANGE:    return "SVC_ERR_OUT_OF_RANGE";
    default:
        /* Fall back to the IDF table for standard codes. */
        return esp_err_to_name(err);
    }
}

uint32_t svc_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}
