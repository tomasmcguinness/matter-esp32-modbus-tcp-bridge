#pragma once

#include <vector>
#include "esp_err.h"
#include "esp_matter.h"
#include "devices_store.h"
#include "modbus_device.h"
#include "solar_power_device.h"
#include "device_readings.h"
#include <esp_matter_bridge.h>

class MatterManager {
public:
    static MatterManager &instance();

    esp_err_t init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator);

    esp_err_t on_device_added(const device_config_t &config, ModbusDevice *modbus);
    esp_err_t on_device_removed(const char *id);
    void      clear();

    uint16_t endpoint_id(const char *id) const;

    bool get_readings(const char *id, DeviceReadings &out) const;

    void on_readings(const char *id, const std::vector<RegisterReading> &readings);

    // endpoint_idx 0 = root, 1..N = parts in order
    struct AttributeMapping {
        uint16_t reg_address;
        uint32_t cluster_id;
        uint32_t attribute_id;
        size_t   endpoint_idx;
    };

    struct EndpointEntry {
        SolarPowerDevice            *matter     = nullptr;
        esp_matter_bridge::device_t *bridge_dev = nullptr;
    };

    struct DevicePair {
        char                          id[DEVICE_ID_LEN];
        std::vector<EndpointEntry>    endpoints; // [0] = root, [1..] = parts
        std::vector<AttributeMapping> mappings;
    };

    MatterManager() = default;

    static esp_err_t device_type_callback(esp_matter::endpoint_t *ep,
                                          uint32_t device_type_id,
                                          void *priv_data);

    esp_err_t create_matter_device(const device_config_t &config, ModbusDevice *modbus);

    EndpointEntry create_or_resume_endpoint(uint32_t device_type_id, uint16_t parent_ep_id,
                                             uint16_t stored_ep_id, const device_config_t &config,
                                             bool &newly_created_out, uint16_t &new_ep_id_out);

    std::vector<AttributeMapping> parse_all_mappings(const char *matter_structure_json);

    esp_matter::node_t     *m_node                   = nullptr;
    uint16_t                m_aggregator_endpoint_id = chip::kInvalidEndpointId;
    std::vector<DevicePair> m_devices;
};
