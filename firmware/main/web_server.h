#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*factory_reset_cb_t)(void);

esp_err_t web_server_start(factory_reset_cb_t on_factory_reset);

#ifdef __cplusplus
}
#endif
