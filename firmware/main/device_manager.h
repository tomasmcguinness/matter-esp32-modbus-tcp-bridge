#pragma once

#include "esp_err.h"
#include "esp_matter.h"
#include "devices_store.h"
#include "device_readings.h"

class DeviceManager {
public:
    static DeviceManager &instance();

    esp_err_t init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator);
    void      start_polling();

    esp_err_t add_device(const char *name, const char *host,
                         uint16_t port, uint8_t unit_id,
                         const char *matter_structure_json,
                         device_config_t *out);

    esp_err_t update_device(const char *id, const char *name, const char *host,
                            uint16_t port, uint8_t unit_id,
                            const char *matter_structure_json,
                            device_config_t *out);

    esp_err_t remove_device(const char *id);

    void clear();

    uint16_t endpoint_id(const char *id) const;

    bool get_readings(const char *id, DeviceReadings &out) const;

private:
    DeviceManager() = default;

    esp_err_t register_device(const device_config_t &config);
    void      unregister_device(const char *id);
};
