#include "modbus_manager.h"

#include <algorithm>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "modbus_manager";

static void voltage_update_cb(uint16_t raw, void *arg)
{
    static_cast<SolarPowerDevice *>(arg)->set_voltage(raw);
}

ModbusManager &ModbusManager::instance()
{
    static ModbusManager s_instance;
    return s_instance;
}

esp_err_t ModbusManager::init(esp_matter::node_t *node)
{
    m_node = node;
    size_t count = devices_store_count();
    for (size_t i = 0; i < count; i++) {
        const device_config_t *cfg = devices_store_at(i);
        auto *modbus = new ModbusDevice(*cfg);
        auto *matter = new SolarPowerDevice(m_node, *cfg);
        modbus->set_voltage_callback(voltage_update_cb, matter);
        modbus->start_polling();
        m_devices.push_back({modbus, matter});
        ESP_LOGI(TAG, "Registered device '%s' (%s:%u)", cfg->name, cfg->host, cfg->port);
    }
    return ESP_OK;
}

esp_err_t ModbusManager::on_device_added(const device_config_t &config)
{
    auto *modbus = new ModbusDevice(config);
    auto *matter = new SolarPowerDevice(m_node, config);
    modbus->set_voltage_callback(voltage_update_cb, matter);
    modbus->start_polling();
    m_devices.push_back({modbus, matter});
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
    for (auto &pair : m_devices) {
        delete pair.modbus;
        delete pair.matter;
    }
    m_devices.clear();
    ESP_LOGI(TAG, "All devices cleared");
}

ModbusDevice *ModbusManager::find(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.modbus->id(), id) == 0; });
    return it != m_devices.end() ? it->modbus : nullptr;
}
