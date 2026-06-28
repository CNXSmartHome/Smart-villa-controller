/**
 * @file svc_common.h
 * @brief Project-wide error type, check macros, and small utilities.
 *
 * Every public SVC-100 function returns ::svc_err_t. The check macros centralize
 * the "evaluate -> on error log + propagate" pattern so no error is dropped and
 * the call sites stay readable. ISR code must NOT use the logging macros.
 */
#ifndef SVC_COMMON_H
#define SVC_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Result type. Alias of esp_err_t so IDF codes pass through unchanged. */
typedef esp_err_t svc_err_t;

/** Base of the project-specific error space (kept clear of IDF/component spaces). */
#define SVC_ERR_BASE (0xB000)

#define SVC_OK                  ESP_OK
#define SVC_ERR_NOT_IMPLEMENTED (SVC_ERR_BASE + 0x01) /**< Feature deferred.        */
#define SVC_ERR_BUS_TIMEOUT     (SVC_ERR_BASE + 0x02) /**< RS485/Modbus turnaround. */
#define SVC_ERR_CRC             (SVC_ERR_BASE + 0x03) /**< Frame CRC mismatch.      */
#define SVC_ERR_MODBUS_EXC      (SVC_ERR_BASE + 0x04) /**< Modbus exception reply.  */
#define SVC_ERR_CONFIG_VERSION  (SVC_ERR_BASE + 0x05) /**< Stored config too new.   */
#define SVC_ERR_OUT_OF_RANGE    (SVC_ERR_BASE + 0x06) /**< Index/arg out of range.  */

/**
 * @brief Evaluate @p expr; if it is not SVC_OK, log and return the error.
 * @note  Requires a file-scope `static const char *TAG`.
 */
#define SVC_RETURN_ON_ERR(expr)                                                   \
    do {                                                                          \
        svc_err_t _err_rc = (expr);                                              \
        if (_err_rc != SVC_OK) {                                                 \
            ESP_LOGE(TAG, "%s:%d %s -> 0x%x", __func__, __LINE__, #expr,         \
                     (int)_err_rc);                                              \
            return _err_rc;                                                       \
        }                                                                         \
    } while (0)

/**
 * @brief Evaluate @p expr; on error, store it in `_err_rc` and `goto cleanup`.
 * @note  Requires a `svc_err_t _err_rc;` and a `cleanup:` label in scope.
 */
#define SVC_GOTO_ON_ERR(expr, label)                                             \
    do {                                                                          \
        _err_rc = (expr);                                                        \
        if (_err_rc != SVC_OK) {                                                 \
            ESP_LOGE(TAG, "%s:%d %s -> 0x%x", __func__, __LINE__, #expr,         \
                     (int)_err_rc);                                              \
            goto label;                                                           \
        }                                                                         \
    } while (0)

/** @brief Return SVC_ERR_OUT_OF_RANGE if @p cond is false. */
#define SVC_CHECK_ARG(cond)                                                      \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ESP_LOGE(TAG, "%s: bad arg (%s)", __func__, #cond);                  \
            return SVC_ERR_OUT_OF_RANGE;                                          \
        }                                                                         \
    } while (0)

/** @brief Human-readable name for an svc_err_t (project codes + IDF fallthrough). */
const char *svc_err_to_name(svc_err_t err);

/** @brief Clamp @p v to the inclusive range [@p lo, @p hi]. */
static inline int32_t svc_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/** @brief Milliseconds since boot (monotonic). */
uint32_t svc_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* SVC_COMMON_H */
