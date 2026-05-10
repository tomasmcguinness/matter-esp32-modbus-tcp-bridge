#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_ID_LEN   17
#define DEVICE_NAME_LEN 64
#define DEVICE_HOST_LEN 64
#define DEVICES_MAX     32

#define MATTER_ENDPOINT_ID_INVALID 0xFFFF

typedef struct {
    char     id[DEVICE_ID_LEN];
    char     name[DEVICE_NAME_LEN];
    char     host[DEVICE_HOST_LEN];
    uint16_t port;
    uint8_t  unit_id;
    uint16_t matter_endpoint_id;
    uint16_t pv1_endpoint_id;
    uint16_t pv2_endpoint_id;
} device_config_t;

esp_err_t devices_store_init(void);

size_t devices_store_count(void);
const device_config_t *devices_store_at(size_t index);

esp_err_t devices_store_add(const char *name,
                            const char *host,
                            uint16_t port,
                            uint8_t unit_id,
                            device_config_t *out);

esp_err_t devices_store_update(const char *id,
                               const char *name,
                               const char *host,
                               uint16_t port,
                               uint8_t unit_id,
                               device_config_t *out);

esp_err_t devices_store_remove(const char *id);
esp_err_t devices_store_clear(void);
esp_err_t devices_store_set_endpoint_id(const char *id, uint16_t endpoint_id);
esp_err_t devices_store_set_pv_endpoint_ids(const char *id, uint16_t pv1_ep, uint16_t pv2_ep);

#ifdef __cplusplus
}
#endif
