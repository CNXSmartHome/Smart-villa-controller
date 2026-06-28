/**
 * @file presence.h
 * @brief mmWave human-presence sensor service (Modbus RTU).
 *
 * Periodically polls a presence sensor over a Modbus master, normalizes the
 * reading to a tri-state, and emits EVT_PRESENCE_CHANGED / EVT_PRESENCE_STALE on
 * the event bus. The sensor register map is configurable so different mmWave
 * modules can be supported without code changes.
 */
#ifndef PRESENCE_H
#define PRESENCE_H

#include "svc_common.h"
#include "modbus_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Normalized presence result. */
typedef enum {
    PRESENCE_UNKNOWN = 0,  /**< No valid reading yet / sensor stale.  */
    PRESENCE_ABSENT  = 1,
    PRESENCE_PRESENT = 2,
} presence_state_t;

/** @brief Sensor register mapping + polling parameters. */
typedef struct {
    mb_master_t *master;       /**< Shared Modbus master (bus owner).        */
    uint8_t      slave_addr;   /**< Modbus slave address of the sensor.      */
    uint16_t     reg_presence; /**< Holding/input register with presence.    */
    uint16_t     present_min;  /**< value >= present_min => PRESENT.         */
    uint8_t      use_input_reg;/**< 1: read input reg (0x04), 0: holding.    */
    uint16_t     poll_ms;      /**< Polling period.                          */
    uint16_t     stale_ms;     /**< No-success window before STALE event.    */
    uint8_t      sensor_id;    /**< Logical id carried in events (0..n).     */
} presence_cfg_t;

/**
 * @brief Start the presence polling task.
 * @param cfg Sensor configuration (copied). Must be non-NULL with a valid master.
 * @return SVC_OK or an error from the RTOS layer.
 */
svc_err_t presence_start(const presence_cfg_t *cfg);

/** @brief Latest normalized presence state (thread-safe snapshot). */
presence_state_t presence_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESENCE_H */
