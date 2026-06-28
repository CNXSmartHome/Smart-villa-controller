/**
 * @file storage.h
 * @brief NVS-backed configuration store.
 *
 * Loads the whole svc_config_t blob once at boot into a caller-owned struct,
 * validates version + CRC, and migrates/falls back to defaults as needed.
 * Saves are explicit. No allocation after init.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "svc_common.h"
#include "svc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the NVS partition used for configuration.
 * @return SVC_OK or an esp_err_t from nvs_flash.
 */
svc_err_t storage_init(void);

/**
 * @brief Load configuration into @p out.
 *
 * On a missing/blank/corrupt/older blob, @p out is filled with factory defaults
 * (and migrated where possible) and SVC_OK is still returned, with @p was_default
 * set true. A stored blob newer than this firmware yields SVC_ERR_CONFIG_VERSION.
 *
 * @param out          Destination config (caller-owned). Must be non-NULL.
 * @param was_default  Optional out-flag: true if defaults were applied.
 * @return SVC_OK or SVC_ERR_CONFIG_VERSION.
 */
svc_err_t storage_load(svc_config_t *out, bool *was_default);

/**
 * @brief Persist @p cfg (CRC recomputed before write).
 * @param cfg Configuration to store. Must be non-NULL.
 * @return SVC_OK or an esp_err_t from nvs.
 */
svc_err_t storage_save(svc_config_t *cfg);

/** @brief Erase the configuration namespace (factory reset). */
svc_err_t storage_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
