#include "modbus_manager.h"

#include <algorithm>
#include <string.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_eth.h"

static const char *TAG = "modbus_manager";

ModbusManager &ModbusManager::instance()
{
    static ModbusManager s_instance;
    return s_instance;
}

void ModbusManager::create_device(const device_config_t &config, const std::vector<RegisterSpec> &regs,
                                   ModbusDevice::readings_cb_t cb)
{
    auto *dev = new ModbusDevice(config, regs);
    if (cb) dev->set_readings_callback(cb, const_cast<char *>(dev->id()));
    m_devices.push_back(dev);
    ESP_LOGI(TAG, "Created Modbus device '%s' (%s:%u)", config.id, config.host, config.port);
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        ModbusManager::instance().pause_all();
    } else if (event_id == ETHERNET_EVENT_CONNECTED) {
        ModbusManager::instance().resume_all();
    }
}

void ModbusManager::init()
{
    // We want to react to Ethernet link events to pause/resume polling, so register an event handler.
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, nullptr);
}

void ModbusManager::start_polling()
{
    for (auto *dev : m_devices) {
        dev->start_polling();
    }
    ESP_LOGI(TAG, "Polling started for %zu device(s)", m_devices.size());
}

void ModbusManager::on_device_added(const device_config_t &config, const std::vector<RegisterSpec> &regs,
                                     ModbusDevice::readings_cb_t cb)
{
    create_device(config, regs, cb);
    ESP_LOGI(TAG, "Device added: %s", config.id);
}

void ModbusManager::on_device_removed(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](ModbusDevice *d) { return strcmp(d->id(), id) == 0; });
    if (it == m_devices.end()) return;
    delete *it;
    m_devices.erase(it);
    ESP_LOGI(TAG, "Device removed: %s", id);
}

void ModbusManager::pause_all()
{
    for (auto *dev : m_devices) {
        dev->pause();
    }
    ESP_LOGI(TAG, "All Modbus devices paused");
}

void ModbusManager::resume_all()
{
    for (auto *dev : m_devices) {
        dev->resume();
    }
    ESP_LOGI(TAG, "All Modbus devices resumed");
}

void ModbusManager::clear()
{
    for (auto *dev : m_devices) {
        delete dev;
    }
    m_devices.clear();
    ESP_LOGI(TAG, "All Modbus devices cleared");
}

ModbusDevice *ModbusManager::find(const char *id) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](ModbusDevice *d) { return strcmp(d->id(), id) == 0; });
    return it != m_devices.end() ? *it : nullptr;
}
