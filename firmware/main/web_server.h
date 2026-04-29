#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*factory_reset_cb_t)(void);

esp_err_t web_server_start(factory_reset_cb_t on_factory_reset);
void web_server_notify_ws_event(const char *event_json);

#ifdef __cplusplus
}
#endif
