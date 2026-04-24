#pragma once

#include <stdint.h>
#include "esp_matter.h"
#include "devices_store.h"

class SolarPowerDevice {
public:
    SolarPowerDevice(esp_matter::node_t *node, const device_config_t &config);

    // raw_value is in 0.1V units (Solax convention); converted to mV internally
    void set_voltage(uint16_t raw_value);

    uint16_t endpoint_id() const { return m_endpoint_id; }

private:
    uint16_t m_endpoint_id;
};
