/**
 * @file modbus_master.h
 * @brief Minimal, allocation-free Modbus RTU master over an RS485 bus.
 *
 * Supports the function codes the SVC-100 needs: read holding/input registers
 * (0x03/0x04) and write single/multiple holding registers (0x06/0x10). CRC-16
 * and exception decoding are handled internally. Each call is one synchronous
 * transaction serialized by the underlying rs485 bus mutex.
 */
#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "svc_common.h"
#include "rs485.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Master instance bound to one RS485 bus. */
typedef struct {
    rs485_handle_t bus;        /**< Open RS485 bus.                            */
    uint32_t       timeout_ms; /**< Per-transaction response timeout.          */
    uint8_t        retries;    /**< Extra attempts on timeout/CRC error.       */
} mb_master_t;

/**
 * @brief Bind a master to an already-open RS485 bus.
 * @param m   Master object to initialize (caller-owned storage).
 * @param bus Open RS485 handle.
 * @param timeout_ms Response timeout per attempt.
 * @param retries    Number of retries on recoverable error.
 * @return SVC_OK or SVC_ERR_OUT_OF_RANGE.
 */
svc_err_t mb_master_init(mb_master_t *m, rs485_handle_t bus,
                         uint32_t timeout_ms, uint8_t retries);

/**
 * @brief Read @p count holding registers (FC 0x03).
 * @param m      Master instance.
 * @param slave  Slave address (1..247).
 * @param addr   Starting register address.
 * @param count  Number of registers (1..125).
 * @param out    Receives @p count 16-bit values (big-endian decoded to host).
 * @return SVC_OK, SVC_ERR_BUS_TIMEOUT, SVC_ERR_CRC, or SVC_ERR_MODBUS_EXC.
 */
svc_err_t mb_read_holding(mb_master_t *m, uint8_t slave, uint16_t addr,
                          uint16_t count, uint16_t *out);

/** @brief Read @p count input registers (FC 0x04). Args mirror mb_read_holding. */
svc_err_t mb_read_input(mb_master_t *m, uint8_t slave, uint16_t addr,
                        uint16_t count, uint16_t *out);

/** @brief Write a single holding register (FC 0x06). */
svc_err_t mb_write_single(mb_master_t *m, uint8_t slave, uint16_t addr,
                          uint16_t value);

/** @brief Write @p count holding registers (FC 0x10). */
svc_err_t mb_write_multiple(mb_master_t *m, uint8_t slave, uint16_t addr,
                            uint16_t count, const uint16_t *values);

/** @brief Compute the Modbus RTU CRC-16 of a buffer (exposed for unit tests). */
uint16_t mb_crc16(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_H */
