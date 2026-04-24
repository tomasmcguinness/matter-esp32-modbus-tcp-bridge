#include "solar_power_device.h"

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_attribute_utils.h"

#include <clusters/ElectricalPowerMeasurement/ClusterId.h>
#include <clusters/ElectricalPowerMeasurement/AttributeIds.h>

static const char *TAG = "solar_power_device";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

SolarPowerDevice::SolarPowerDevice(node_t *node, const device_config_t &config)
    : m_endpoint_id(0)
{
    solar_power::config_t cfg;
    endpoint_t *ep = solar_power::create(node, &cfg, ENDPOINT_FLAG_DESTROYABLE, nullptr);
    if (!ep) {
        ESP_LOGE(TAG, "[%s] Failed to create solar_power endpoint", config.id);
        return;
    }
    m_endpoint_id = endpoint::get_id(ep);
    ESP_LOGI(TAG, "[%s] Solar Power endpoint created: ep=%u", config.id, m_endpoint_id);
}

void SolarPowerDevice::set_voltage(uint16_t raw_value)
{
    // Solax reports voltage in 0.1V units; Matter expects millivolts
    int64_t millivolts = (int64_t)raw_value * 100;

    esp_matter_attr_val_t val = esp_matter_nullable_int64(nullable<int64_t>(millivolts));
    esp_matter::attribute::update(
        m_endpoint_id,
        ElectricalPowerMeasurement::Id,
        ElectricalPowerMeasurement::Attributes::Voltage::Id,
        &val);
}
