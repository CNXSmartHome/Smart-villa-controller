/**
 * @file rs485.h
 * @brief Half-duplex RS485 transport over a UART with DE/RE direction control.
 *
 * Provides a single transactional primitive rs485_txn(): drive the bus, send a
 * request, switch to receive, and read a response within a timeout. The UART is
 * configured in IDF hardware RS485 half-duplex mode so the transceiver DE line
 * is toggled by the UART (RTS) automatically around each frame — no manual,
 * timing-sensitive GPIO toggling in software.
 */
#ifndef RS485_H
#define RS485_H

#include "svc_common.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One RS485 bus instance handle (opaque to callers). */
typedef struct rs485_bus *rs485_handle_t;

/** @brief Bus configuration. */
typedef struct {
    board_rs485_id_t id;        /**< Selects pins/UART via the board map.       */
    uint32_t         baud;      /**< e.g. 9600 / 19200 / 115200.                */
    uint8_t          data_bits; /**< 7 or 8.                                    */
    uint8_t          stop_bits; /**< 1 or 2.                                    */
    char             parity;    /**< 'N', 'E', or 'O'.                          */
} rs485_cfg_t;

/**
 * @brief Open and configure an RS485 bus instance.
 * @param cfg Bus configuration. Must be non-NULL.
 * @param out Receives the bus handle on success.
 * @return SVC_OK or an error from the UART driver.
 */
svc_err_t rs485_open(const rs485_cfg_t *cfg, rs485_handle_t *out);

/**
 * @brief Perform a half-duplex request/response transaction.
 *
 * The call is serialized per bus by an internal mutex, so multiple services may
 * share one physical line safely. It flushes stale RX, transmits @p req, waits
 * for the line turnaround, then reads up to @p resp_cap bytes until the
 * inter-frame gap or @p timeout_ms elapses.
 *
 * @param h          Bus handle from rs485_open().
 * @param req        Request bytes to transmit.
 * @param req_len    Number of request bytes.
 * @param resp       Buffer for the response (may be NULL if resp_cap == 0).
 * @param resp_cap   Capacity of @p resp.
 * @param out_len    Receives the number of response bytes read.
 * @param timeout_ms Overall response timeout.
 * @return SVC_OK, SVC_ERR_BUS_TIMEOUT, or a UART error.
 */
svc_err_t rs485_txn(rs485_handle_t h,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap, size_t *out_len,
                    uint32_t timeout_ms);

/** @brief Close a bus and release its UART driver. */
svc_err_t rs485_close(rs485_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* RS485_H */
