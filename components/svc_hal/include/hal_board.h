/**
 * @file hal_board.h
 * @brief Smart Villa OS Core — Hardware Abstraction Layer (board interface).
 *
 * Target-neutral contract used by the services/logic/api layers. Exactly one
 * target component (hal_svc100 or hal_kincony) installs a ::hal_driver_t and a
 * ::board_profile_t at boot via hal_board_register(); the common dispatcher in
 * hal_board.c validates indices, keeps the relay shadow mask, and forwards to the
 * driver. The dispatcher is pure (no ESP-IDF) so it is host-unit-tested with a
 * mock driver.
 *
 * Masks are 32-bit: up to HAL_MAX_RELAY relays / HAL_MAX_DIN inputs.
 */
#ifndef HAL_BOARD_H
#define HAL_BOARD_H

#include "svc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MAX_RELAY   32
#define HAL_MAX_DIN     32
#define HAL_MAX_RS485    4
#define HAL_BOARD_ID_MAX 32

/* Capability bits (hal_board_get_capabilities()). */
#define HAL_CAP_RTC          (1u << 0)
#define HAL_CAP_ETHERNET     (1u << 1)
#define HAL_CAP_BUZZER       (1u << 2)
#define HAL_CAP_RGB_LED      (1u << 3)
#define HAL_CAP_IO_EXPANDER  (1u << 4)  /* relays/inputs behind I2C expander */
#define HAL_CAP_NATIVE_USB   (1u << 5)
#define HAL_CAP_PRESENCE_485 (1u << 6)  /* mmWave presence over RS485 Modbus */
#define HAL_CAP_PROV_BUTTON  (1u << 7)  /* physical provisioning button       */

/** @brief Relay drive polarity (how the target maps logical ON to hardware). */
typedef enum {
    RELAY_ACTIVE_HIGH = 0,
    RELAY_ACTIVE_LOW  = 1,
} relay_polarity_t;

/** @brief Safe-state policy. Only DE-ENERGIZED is permitted (Project Bible). */
typedef enum {
    SAFE_STATE_DEENERGIZED = 0,
} safe_state_policy_t;

/** @brief OTA / boot health policy, sourced from the board profile. */
typedef struct {
    bool     require_presence_modbus; /**< presence MUST run to validate (SVC-100). */
    bool     allow_presence_degraded; /**< a missing/stale sensor is OK (KinCony).   */
    bool     require_relay_safe;      /**< relay safe-state must be applied.          */
    uint32_t boot_window_ms;          /**< max wait for health before giving up.      */
} board_health_policy_t;

/** @brief Static board description (one per target/variant). */
typedef struct {
    char     board_id[HAL_BOARD_ID_MAX];   /**< stable slug, e.g. "kincony_kc868_a8". */
    const char *board_name;                /**< human name.                           */
    uint8_t  relay_count;                  /**< <= HAL_MAX_RELAY.                      */
    uint8_t  din_count;                    /**< <= HAL_MAX_DIN.                        */
    uint8_t  rs485_count;                  /**< <= HAL_MAX_RS485.                      */
    bool     has_presence_modbus;
    bool     has_physical_provision_button;
    const char *const *relay_labels;       /**< [relay_count], may be NULL.           */
    const char *const *din_labels;         /**< [din_count], may be NULL.             */
    relay_polarity_t   relay_polarity;
    safe_state_policy_t safe_state;
    board_health_policy_t health;
    uint32_t caps;
} board_profile_t;

/** @brief RS485 UART + pin config for one port. */
typedef struct {
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int de_gpio;   /**< -1 = auto/none. */
    uint32_t default_baud;
} hal_rs485_config_t;

/**
 * @brief Target driver vtable. A target provides raw, polarity-applied IO; the
 *        common dispatcher does index validation and the relay shadow.
 *
 * SAFETY: apply_safe_state MUST drive all relays de-energized as a single
 * operation and return an error if it cannot guarantee that (e.g. I2C fault on
 * an expander board) — the caller treats a failure as a hard fault.
 */
typedef struct {
    svc_err_t (*init)(void);
    svc_err_t (*relay_write)(uint8_t idx, bool on);
    svc_err_t (*relay_write_mask)(uint32_t energized_mask);
    svc_err_t (*relay_read)(uint8_t idx, bool *on);
    svc_err_t (*din_read)(uint8_t idx, bool *raw_high);
    uint32_t  (*din_read_mask)(void);
    svc_err_t (*apply_safe_state)(void);
    svc_err_t (*rs485_get_config)(uint8_t port, hal_rs485_config_t *out);
} hal_driver_t;

/* ----- target installation (called once by the linked hal_<target>) ----- */

/**
 * @brief Install the linked build target's HAL driver + profile.
 *
 * PROVIDED BY the single linked hal_<target> component (e.g. hal_svc100), which
 * calls hal_board_register() with its own driver + board_profile_t. app_main
 * calls hal_install() once at boot, then hal_board_init(). Fails closed if the
 * target build did not link a HAL implementation (linker error) or registration
 * is rejected.
 */
svc_err_t hal_install(void);

/** @brief Register the active driver + profile. Validates counts <= HAL_MAX_*. */
svc_err_t hal_board_register(const hal_driver_t *drv, const board_profile_t *profile);

/* ----- common API used by services/logic/api ----- */

svc_err_t                hal_board_init(void);
const board_profile_t   *hal_board_get_info(void);
uint32_t                 hal_board_get_capabilities(void);
const board_health_policy_t *hal_board_get_health_policy(void);

/** @brief Drive all relays de-energized atomically (failsafe). */
svc_err_t hal_board_apply_safe_state(void);

uint8_t   hal_relay_count(void);
svc_err_t hal_relay_set(uint8_t index, bool state);
svc_err_t hal_relay_get(uint8_t index, bool *state);
svc_err_t hal_relay_set_mask(uint32_t energized_mask);
uint32_t  hal_relay_get_mask(void);

uint8_t   hal_din_count(void);
svc_err_t hal_din_read(uint8_t index, bool *state);
uint32_t  hal_din_read_mask(void);

svc_err_t hal_rs485_get_config(uint8_t port, hal_rs485_config_t *config);

/**
 * @brief Profile-driven OTA/boot health gate (pure; host-tested).
 *
 * Decides whether the running image may be marked valid, given the board's
 * health policy and the observed boot signals. SVC-100 requires presence to have
 * run; KinCony allows a degraded/absent presence sensor. Relay-safe and a clean
 * watchdog are always required when the policy demands them.
 *
 * @return true if the image may be confirmed.
 */
bool board_health_ota_ok(const board_health_policy_t *policy,
                         bool control_alive, bool presence_ran,
                         bool relay_safe_applied, bool net_settled,
                         bool wdt_stable, bool fault_latched);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BOARD_H */
