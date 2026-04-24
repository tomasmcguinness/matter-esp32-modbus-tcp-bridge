#pragma once

#include <vector>
#include "esp_err.h"
#include "esp_matter.h"
#include "devices_store.h"
#include "modbus_device.h"
#include "solar_power_device.h"

class ModbusManager {
public:
    static ModbusManager &instance();

    esp_err_t init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator);

    esp_err_t on_device_added(const device_config_t &config);
    esp_err_t on_device_removed(const char *id);
    esp_err_t on_device_updated(const device_config_t &config);

    ModbusDevice *find(const char *id);
    void          clear();

private:
    struct DevicePair {
        ModbusDevice      *modbus;
        SolarPowerDevice  *matter;
    };

    ModbusManager() = default;
    esp_matter::node_t       *m_node       = nullptr;
    esp_matter::endpoint_t   *m_aggregator = nullptr;
    std::vector<DevicePair>   m_devices;
};
