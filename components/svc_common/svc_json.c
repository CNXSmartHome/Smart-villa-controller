/**
 * @file svc_json.c
 * @brief JSON output helpers implementation (see svc_json.h).
 */
#include "svc_json.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "svc_json";

svc_err_t svc_json_escape_n(const char *src, size_t src_len,
                            char *dst, size_t dst_len)
{
    SVC_CHECK_ARG(dst != NULL && dst_len >= 1);
    size_t o = 0;

    /* Helper: append a raw byte, guarding capacity (reserve 1 for the NUL). */
    #define PUT(ch)                                  \
        do {                                         \
            if (o + 1 >= dst_len) { goto overflow; } \
            dst[o++] = (char)(ch);                   \
        } while (0)

    if (src != NULL) {
        /* Iterate by index up to src_len so we NEVER read past the source
           buffer, even if it is not NUL-terminated. An embedded NUL still ends
           the logical string. */
        for (size_t i = 0; i < src_len; ++i) {
            unsigned char c = (unsigned char)src[i];
            if (c == '\0') {
                break;
            }
            switch (c) {
            case '\"': PUT('\\'); PUT('\"'); break;
            case '\\': PUT('\\'); PUT('\\'); break;
            case '/':  PUT('\\'); PUT('/');  break;
            case '\b': PUT('\\'); PUT('b');  break;
            case '\f': PUT('\\'); PUT('f');  break;
            case '\n': PUT('\\'); PUT('n');  break;
            case '\r': PUT('\\'); PUT('r');  break;
            case '\t': PUT('\\'); PUT('t');  break;
            default:
                if (c < 0x20) {
                    /* Control character -> \u00XX */
                    static const char hex[] = "0123456789abcdef";
                    PUT('\\'); PUT('u'); PUT('0'); PUT('0');
                    PUT(hex[(c >> 4) & 0xF]);
                    PUT(hex[c & 0xF]);
                } else {
                    PUT(c);
                }
                break;
            }
        }
    }
    dst[o] = '\0';
    return SVC_OK;

overflow:
    dst[0] = '\0';
    ESP_LOGW(TAG, "escape overflow (cap=%u)", (unsigned)dst_len);
    return SVC_ERR_OUT_OF_RANGE;
    #undef PUT
}

svc_err_t svc_json_escape(const char *in, char *out, size_t out_cap)
{
    /* Bounded by strlen for a known-terminated C string. For fixed-size config
       arrays callers MUST use svc_json_escape_n with strnlen() instead. */
    return svc_json_escape_n(in, in ? strlen(in) : 0, out, out_cap);
}
