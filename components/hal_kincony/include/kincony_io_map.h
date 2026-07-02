/**
 * @file kincony_io_map.h
 * @brief PURE PCF8574 relay/input mapping for KinCony KC868 boards.
 *
 * No ESP-IDF: this is the bit/byte mapping between logical relay/input indices
 * and the PCF8574 expander chips. Host-unit-tested. The I2C transport lives in
 * hal_kincony.c.
 *
 * !!! RELEASE GATE (RG-K1): the relay drive polarity and the PCF8574 power-on /
 * reset default output state are DESIGN ASSUMPTIONS here and MUST be verified on
 * a physical KinCony board before any relay is trusted. KinCony relay modules
 * are commonly ACTIVE-LOW (writing 0 energizes), so the safe/idle byte is 0xFF.
 * If a board differs, the safe byte and polarity below are WRONG and unsafe.
 */
#ifndef KINCONY_IO_MAP_H
#define KINCONY_IO_MAP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KINCONY_MAX_CHIPS 4   /* 4x8 = 32 channels max */

/** @brief PCF8574 expander layout for a KinCony variant. */
typedef struct {
    uint8_t relay_addr[KINCONY_MAX_CHIPS]; /**< I2C 7-bit addr of each relay chip. */
    uint8_t relay_chips;                   /**< number of relay PCF8574s.           */
    uint8_t input_addr[KINCONY_MAX_CHIPS]; /**< I2C addr of each input chip.        */
    uint8_t input_chips;                   /**< number of input PCF8574s.           */
    bool    relay_active_low;              /**< true: write 0 energizes (idle=0xFF). */
    bool    input_active_low;              /**< true: asserted input reads 0.        */
} kincony_io_map_t;

/** @brief Locate logical relay @p idx -> chip index + bit (0..7). */
static inline bool kincony_relay_locate(const kincony_io_map_t *m, uint8_t idx,
                                        uint8_t *chip, uint8_t *bit)
{
    if (idx >= m->relay_chips * 8u) return false;
    *chip = idx / 8u; *bit = idx % 8u; return true;
}

/**
 * @brief The PCF8574 output byte that drives ALL relays de-energized (safe).
 * @return 0xFF for active-low relay boards, 0x00 for active-high.
 */
static inline uint8_t kincony_relay_safe_byte(const kincony_io_map_t *m)
{
    return m->relay_active_low ? 0xFFu : 0x00u;
}

/**
 * @brief Pack a 32-bit logical energized mask into per-chip PCF8574 output bytes.
 *
 * @param m       Board IO map.
 * @param mask    bit i = relay i ENERGIZED.
 * @param out     Receives one byte per relay chip (>= relay_chips entries).
 */
void kincony_pack_relays(const kincony_io_map_t *m, uint32_t mask, uint8_t *out);

/**
 * @brief Convert per-chip PCF8574 input bytes into a 32-bit logical input mask.
 *        bit i = input i ASSERTED (active level, after input_active_low).
 *
 * @param m   Board IO map.
 * @param in  One raw byte per input chip (>= input_chips entries).
 * @return logical asserted mask.
 */
uint32_t kincony_unpack_inputs(const kincony_io_map_t *m, const uint8_t *in);

#ifdef __cplusplus
}
#endif

#endif /* KINCONY_IO_MAP_H */
