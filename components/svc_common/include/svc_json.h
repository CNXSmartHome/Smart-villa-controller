/**
 * @file svc_json.h
 * @brief Minimal, allocation-free JSON output helpers.
 *
 * The escape helper is mandatory for any string that originates from
 * configuration or the network (device name, SSID, etc.) before it is placed
 * inside a JSON document. Never concatenate raw strings into JSON: an unescaped
 * quote or backslash breaks the document and enables injection.
 */
#ifndef SVC_JSON_H
#define SVC_JSON_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Escape exactly @p src_len bytes of @p src as a JSON string body.
 *
 * This is the safe primitive: it NEVER reads past @p src_len, so it is correct
 * even when @p src is a fixed-size config array that is not NUL-terminated.
 * Escaping stops early at an embedded NUL (treated as end-of-string). Escapes
 * `"`, `\`, `/`, and all control characters (< 0x20). The result is always
 * NUL-terminated. If the escaped form (plus terminator) does not fit in
 * @p dst_len, @p dst is terminated empty and SVC_ERR_OUT_OF_RANGE is returned.
 *
 * @param src      Source bytes (may be NULL -> empty output).
 * @param src_len  Maximum number of bytes to read from @p src.
 * @param dst      Destination buffer.
 * @param dst_len  Capacity of @p dst (must be >= 1).
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t svc_json_escape_n(const char *src, size_t src_len,
                            char *dst, size_t dst_len);

/**
 * @brief Convenience wrapper that escapes a NUL-terminated C string.
 *
 * Equivalent to svc_json_escape_n(in, strlen(in), out, out_cap). Do NOT use this
 * on fixed-size config arrays that may be un-terminated — use
 * svc_json_escape_n() with strnlen(arr, sizeof(arr)) instead.
 *
 * @param in       Source C string (may be NULL -> empty output).
 * @param out      Destination buffer.
 * @param out_cap  Capacity of @p out in bytes (must be >= 1).
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t svc_json_escape(const char *in, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* SVC_JSON_H */
