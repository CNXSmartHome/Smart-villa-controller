/**
 * @file eventbus.h
 * @brief Decoupled, fixed-size event queue connecting producers (dinput,
 *        presence, button, netmgr) to the control task.
 *
 * Events are value types only: no pointers to transient memory cross the bus,
 * so there are no ownership/lifetime concerns and no post-boot allocation.
 */
#ifndef EVENTBUS_H
#define EVENTBUS_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event categories carried on the bus. */
typedef enum {
    EVT_NONE = 0,
    EVT_BUTTON_SHORT,     /**< arg0 = button id (0=config, 1=reset)            */
    EVT_BUTTON_LONG,      /**< arg0 = button id                                */
    EVT_DINPUT_EDGE,      /**< arg0 = channel, arg1 = level (0/1)              */
    EVT_PRESENCE_CHANGED, /**< arg0 = presence_state_t, arg1 = sensor id       */
    EVT_PRESENCE_STALE,   /**< arg0 = sensor id (poll timeout)                 */
    EVT_NET_UP,           /**< arg0 = iface (0=wifi,1=eth)                     */
    EVT_NET_DOWN,         /**< arg0 = iface                                    */
    EVT_CONFIG_CHANGED,   /**< config persisted; consumers may reload          */
    EVT_OTA_PROGRESS,     /**< arg0 = percent                                  */
    EVT_TYPE_MAX,
} svc_event_type_t;

/** @brief One fixed-size bus message (16 bytes). */
typedef struct {
    svc_event_type_t type;   /**< Event category.                              */
    uint8_t          arg0;   /**< Primary argument (channel/id/state).         */
    uint8_t          arg1;   /**< Secondary argument (level/iface).            */
    uint16_t         arg2;   /**< Optional 16-bit payload.                     */
    uint32_t         t_ms;   /**< Producer timestamp (svc_now_ms()).           */
} svc_event_t;

/**
 * @brief Create the event queue. Call once during boot before any producer.
 * @param depth Number of queued events to buffer (e.g. 24).
 * @return SVC_OK or ESP_ERR_NO_MEM.
 */
svc_err_t eventbus_init(size_t depth);

/**
 * @brief Post an event from task context.
 * @param ev          Event to copy onto the queue (fields auto-stamped if t_ms==0).
 * @param timeout_ms  Max time to wait for queue space.
 * @return SVC_OK, or ESP_ERR_TIMEOUT if the queue stayed full.
 */
svc_err_t eventbus_post(const svc_event_t *ev, uint32_t timeout_ms);

/**
 * @brief Post an event from ISR context (no logging, no blocking).
 * @param ev               Event to copy.
 * @param hp_task_woken    Set true if a higher-prio task was woken (pass to
 *                         portYIELD_FROM_ISR). May be NULL.
 * @return SVC_OK or ESP_FAIL if the queue was full.
 */
svc_err_t eventbus_post_isr(const svc_event_t *ev, bool *hp_task_woken);

/**
 * @brief Block until an event is available (consumer side, control task).
 * @param out         Receives the dequeued event.
 * @param timeout_ms  Max wait (use portMAX_DELAY-equivalent UINT32_MAX to block).
 * @return SVC_OK, or ESP_ERR_TIMEOUT if no event arrived.
 */
svc_err_t eventbus_receive(svc_event_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EVENTBUS_H */
