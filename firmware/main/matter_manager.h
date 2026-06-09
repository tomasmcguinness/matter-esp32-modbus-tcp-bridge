#pragma once

#include <vector>
#include "esp_err.h"
#include "esp_matter.h"
#include "devices_store.h"
#include "modbus_device.h"
#include "solar_power_device.h"
#include "electrical_sensor_device.h"
#include "device_readings.h"
#include <esp_matter_bridge.h>

class MatterManager {
public:
    static MatterManager &instance();

    esp_err_t init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator);

    esp_err_t on_device_added(const device_config_t &config);
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
        IMatterDevice *matter_dev = nullptr;
        // Root endpoints are bridged devices (have bridge_dev). Part endpoints are plain composed
        // endpoints parented to the root (bridge_dev == nullptr). `endpoint` is always set.
        esp_matter_bridge::device_t *bridge_dev = nullptr;
        esp_matter::endpoint_t      *endpoint   = nullptr;
    };

    struct BridgedMatterDevice {
        char                          id[DEVICE_ID_LEN];
        std::vector<EndpointEntry>    endpoints; // [0] = root, [1..] = parts
        std::vector<AttributeMapping> mappings;
    };

    MatterManager() = default;

    static esp_err_t device_type_callback(esp_matter::endpoint_t *ep,
                                          uint32_t device_type_id,
                                          void *priv_data);

    esp_err_t create_matter_device(const device_config_t &config);

    // Root device: a bridged device parented to the aggregator (via esp_matter_bridge).
    EndpointEntry create_or_resume_endpoint(uint32_t device_type_id, uint16_t parent_ep_id,
                                             uint16_t stored_ep_id, const device_config_t &config,
                                             bool &newly_created_out, uint16_t &new_ep_id_out);

    // Part device: a plain composed endpoint parented to the root endpoint. The bridge API only
    // allows aggregator-parented devices, so parts cannot be bridged devices.
    EndpointEntry create_or_resume_part(const std::vector<uint32_t> &device_types, esp_matter::endpoint_t *parent_ep,
                                        uint16_t stored_ep_id, const device_config_t &config,
                                        bool &newly_created_out, uint16_t &new_ep_id_out);

    std::vector<AttributeMapping> parse_all_mappings(const char *matter_structure_json);

    esp_matter::node_t     *m_node                   = nullptr;
    uint16_t                m_aggregator_endpoint_id = chip::kInvalidEndpointId;
    std::vector<BridgedMatterDevice> m_devices;
};
