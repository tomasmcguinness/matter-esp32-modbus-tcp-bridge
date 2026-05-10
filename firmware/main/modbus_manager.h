#pragma once

#include <vector>
#include "esp_err.h"
#include "esp_matter.h"
#include "devices_store.h"
#include "modbus_device.h"
#include "solar_power_device.h"
#include "electrical_sensor_device.h"
#include <esp_matter_bridge.h>

class ModbusManager {
public:
    static ModbusManager &instance();

    esp_err_t init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator);
    void      start_polling();

    esp_err_t on_device_added(const device_config_t &config);
    esp_err_t on_device_removed(const char *id);
    esp_err_t on_device_updated(const device_config_t &config);

    struct DeviceReadings {
        bool    voltage_valid;
        int64_t voltage_mv;
        bool    current_valid;
        int64_t current_ma;
        bool    power_valid;
        int64_t power_mw;
        bool    pv1_voltage_valid;
        int64_t pv1_voltage_mv;
        bool    pv1_current_valid;
        int64_t pv1_current_ma;
        bool    pv2_voltage_valid;
        int64_t pv2_voltage_mv;
        bool    pv2_current_valid;
        int64_t pv2_current_ma;
    };

    struct ReadingsArg {
        SolarPowerDevice       *solar;
    };

    ModbusDevice *find(const char *id);
    uint16_t      endpoint_id(const char *id) const;
    bool          get_readings(const char *id, DeviceReadings &out) const;
    void          clear();

private:

    struct DevicePair {
        ModbusDevice                *modbus;
        SolarPowerDevice            *matter;
        //ElectricalSensorDevice      *pv1;
        //ElectricalSensorDevice      *pv2;
        esp_matter_bridge::device_t *bridge_dev;
        //esp_matter_bridge::device_t *pv1_bridge_dev;
        //esp_matter_bridge::device_t *pv2_bridge_dev;
        ReadingsArg                 *readings_arg;
    };

    ModbusManager() = default;

    static esp_err_t device_type_callback(esp_matter::endpoint_t *ep,
                                          uint32_t device_type_id,
                                          void *priv_data);

    esp_err_t register_device(const device_config_t &config);

    esp_matter::node_t       *m_node                   = nullptr;
    uint16_t                  m_aggregator_endpoint_id = chip::kInvalidEndpointId;
    std::vector<DevicePair>   m_devices;
};
