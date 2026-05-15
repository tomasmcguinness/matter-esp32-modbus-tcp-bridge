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

#define MATTER_ENDPOINT_ID_INVALID   0xFFFF
#define MATTER_STRUCTURE_JSON_LEN    1024

typedef struct {
    char     id[DEVICE_ID_LEN];
    char     name[DEVICE_NAME_LEN];
    char     host[DEVICE_HOST_LEN];
    uint16_t port;
    uint8_t  unit_id;
    char     matter_structure_json[MATTER_STRUCTURE_JSON_LEN];
} device_config_t;

esp_err_t devices_store_init(void);

size_t devices_store_count(void);
const device_config_t *devices_store_at(size_t index);

esp_err_t devices_store_add(const char *name,
                            const char *host,
                            uint16_t port,
                            uint8_t unit_id,
                            const char *matter_structure_json,
                            device_config_t *out);

esp_err_t devices_store_update(const char *id,
                               const char *name,
                               const char *host,
                               uint16_t port,
                               uint8_t unit_id,
                               const char *matter_structure_json,
                               device_config_t *out);

esp_err_t devices_store_remove(const char *id);
esp_err_t devices_store_clear(void);
esp_err_t devices_store_update_matter_structure(const char *id, const char *matter_structure_json);

const device_config_t *devices_store_find(const char *id);

#ifdef __cplusplus
}
#endif
