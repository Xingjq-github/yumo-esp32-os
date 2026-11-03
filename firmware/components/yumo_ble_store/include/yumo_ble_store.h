#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Check immediately after ble_store_config_init(), before starting the host.
// A load/migration error must abort BLE startup, never create an empty store.
esp_err_t yumo_ble_store_status(void);

#ifdef __cplusplus
}
#endif
