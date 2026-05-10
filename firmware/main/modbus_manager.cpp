#include "modbus_manager.h"

#include <algorithm>
#include <string.h>

#include "esp_log.h"
#include <esp_matter_bridge.h>
#include <esp_matter_endpoint.h>
#include <platform/CHIPDeviceLayer.h>

#include <clusters/BridgedDeviceBasicInformation/ClusterId.h>

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;

static const char *TAG = "modbus_manager";

static void readings_update_cb(const ModbusDevice::Readings &r, void *arg)
{
    auto *dev = static_cast<SolarPowerDevice *>(arg);
    dev->set_voltage(r.voltage_raw);
    dev->set_active_current(r.current_raw);
    dev->set_active_power(r.power_raw);
}

ModbusManager &ModbusManager::instance()
{
    static ModbusManager s_instance;
    return s_instance;
}

esp_err_t ModbusManager::device_type_callback(esp_matter::endpoint_t *ep,
                                              uint32_t device_type_id,
                                              void *priv_data)
{
    if (device_type_id != ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID) {
        ESP_LOGE(TAG, "Unsupported device type: 0x%08" PRIx32, device_type_id);
        return ESP_ERR_INVALID_ARG;
    }

    solar_power::config_t sp_cfg;
    if (solar_power::add(ep, &sp_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "solar_power::add failed");
        return ESP_FAIL;
    }

    const device_config_t *cfg = static_cast<const device_config_t *>(priv_data);
    if (cfg) {
        cluster_t *bdbi = cluster::get(ep, chip::app::Clusters::BridgedDeviceBasicInformation::Id);
        if (bdbi) {
            bridged_device_basic_information::attribute::create_node_label(
                bdbi, (char *)cfg->name, strlen(cfg->name));
        }
    }

    return ESP_OK;
}

esp_err_t ModbusManager::register_device(const device_config_t &config)
{
    esp_matter_bridge::device_t *dev = nullptr;

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    if (config.matter_endpoint_id != MATTER_ENDPOINT_ID_INVALID) {
        dev = esp_matter_bridge::resume_device(m_node, config.matter_endpoint_id,
                                               const_cast<device_config_t *>(&config));
        if (!dev) {
            ESP_LOGW(TAG, "[%s] resume_device(ep=%u) failed, creating new endpoint",
                     config.id, config.matter_endpoint_id);
        }
    }

    if (!dev) {
        dev = esp_matter_bridge::create_device(m_node, m_aggregator_endpoint_id,
                                               ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID,
                                               const_cast<device_config_t *>(&config));
        if (!dev) {
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
            ESP_LOGE(TAG, "[%s] create_device failed", config.id);
            return ESP_FAIL;
        }
        uint16_t ep_id = endpoint::get_id(dev->endpoint);
        devices_store_set_endpoint_id(config.id, ep_id);
    }

    endpoint::enable(dev->endpoint);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    auto *modbus = new ModbusDevice(config);
    auto *matter = new SolarPowerDevice(dev, config);
    modbus->set_readings_callback(readings_update_cb, matter);
    m_devices.push_back({modbus, matter, dev});

    ESP_LOGI(TAG, "Registered '%s' (%s:%u) -> ep=%u",
             config.name, config.host, config.port, matter->endpoint_id());
    return ESP_OK;
}

esp_err_t ModbusManager::init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator)
{
    m_node                 = node;
    m_aggregator_endpoint_id = endpoint::get_id(aggregator);

    esp_err_t err = esp_matter_bridge::initialize(node, device_type_callback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter_bridge::initialize failed: %d", err);
        return err;
    }

    size_t count = devices_store_count();
    for (size_t i = 0; i < count; i++) {
        const device_config_t *cfg = devices_store_at(i);
        if (register_device(*cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register device '%s'", cfg->id);
        }
    }

    return ESP_OK;
}

void ModbusManager::start_polling()
{
    for (auto &pair : m_devices) {
        pair.modbus->start_polling();
    }
    ESP_LOGI(TAG, "Polling started for %zu device(s)", m_devices.size());
}

esp_err_t ModbusManager::on_device_added(const device_config_t &config)
{
    esp_err_t err = register_device(config);
    if (err != ESP_OK) return err;

    m_devices.back().modbus->start_polling();
    ESP_LOGI(TAG, "Device added: %s", config.id);
    return ESP_OK;
}

esp_err_t ModbusManager::on_device_removed(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.modbus->id(), id) == 0; });

    if (it == m_devices.end()) return ESP_ERR_NOT_FOUND;

    delete it->modbus;
    delete it->matter;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_matter_bridge::remove_device(it->bridge_dev);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    m_devices.erase(it);
    ESP_LOGI(TAG, "Device removed: %s", id);
    return ESP_OK;
}

esp_err_t ModbusManager::on_device_updated(const device_config_t &config)
{
    on_device_removed(config.id);
    return on_device_added(config);
}

void ModbusManager::clear()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    for (auto &pair : m_devices) {
        delete pair.modbus;
        delete pair.matter;
        esp_matter_bridge::remove_device(pair.bridge_dev);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    m_devices.clear();
    ESP_LOGI(TAG, "All devices cleared");
}

ModbusDevice *ModbusManager::find(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.modbus->id(), id) == 0; });
    return it != m_devices.end() ? it->modbus : nullptr;
}

uint16_t ModbusManager::endpoint_id(const char *id) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.modbus->id(), id) == 0; });
    return it != m_devices.end() ? it->matter->endpoint_id() : 0;
}

bool ModbusManager::get_readings(const char *id, DeviceReadings &out) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.modbus->id(), id) == 0; });
    if (it == m_devices.end()) return false;
    out.voltage_valid = it->matter->get_voltage_mv(out.voltage_mv);
    out.current_valid = it->matter->get_active_current_ma(out.current_ma);
    out.power_valid   = it->matter->get_active_power_mw(out.power_mw);
    return true;
}
