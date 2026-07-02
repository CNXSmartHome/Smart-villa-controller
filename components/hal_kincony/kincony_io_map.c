/**
 * @file kincony_io_map.c
 * @brief Pure PCF8574 mapping implementation (see kincony_io_map.h).
 */
#include "kincony_io_map.h"

void kincony_pack_relays(const kincony_io_map_t *m, uint32_t mask, uint8_t *out)
{
    for (uint8_t c = 0; c < m->relay_chips; ++c) {
        uint8_t energized = (uint8_t)((mask >> (c * 8u)) & 0xFFu);
        /* Active-low relay boards: write 0 to energize, so the wire byte is the
           inverse of the energized bits (idle/safe = 0xFF). */
        out[c] = m->relay_active_low ? (uint8_t)~energized : energized;
    }
}

uint32_t kincony_unpack_inputs(const kincony_io_map_t *m, const uint8_t *in)
{
    uint32_t asserted = 0;
    for (uint8_t c = 0; c < m->input_chips; ++c) {
        uint8_t raw = in[c];
        /* Active-low input: a closed contact reads 0 -> asserted. */
        uint8_t a = m->input_active_low ? (uint8_t)~raw : raw;
        asserted |= ((uint32_t)a) << (c * 8u);
    }
    return asserted;
}
