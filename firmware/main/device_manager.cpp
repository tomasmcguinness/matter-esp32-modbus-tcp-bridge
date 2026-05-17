#include "device_manager.h"
#include "modbus_manager.h"
#include "matter_manager.h"

#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "device_manager";

static void readings_dispatch_cb(const std::vector<RegisterReading> &readings, void *arg)
{
    // We have received new readings from a Modbus device.
    // Locate the Matter Device and update its attributes accordingly.
    //
    MatterManager::instance().on_readings(static_cast<const char *>(arg), readings);
}

DeviceManager &DeviceManager::instance()
{
    static DeviceManager s_instance;
    return s_instance;
}

esp_err_t DeviceManager::init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator)
{
    ESP_LOGI(TAG, "Initializing...");

    ModbusManager::instance().init();
    MatterManager::instance().init(node, aggregator);

    size_t count = devices_store_count();

    ESP_LOGI(TAG, "There are %u devices to initialize...", count);

    for (size_t i = 0; i < count; i++) {

        const device_config_t *cfg = devices_store_at(i);

        // For each device in the store, we need to create a ModbusDevice
        // and a series of Matter endpoints according to the stored matter_structure_json.
        //
        ESP_LOGI(TAG, "Initializing device '%s' from store with id '%s'...", cfg->name, cfg->id);

        ModbusManager::instance().on_device_added(*cfg, readings_dispatch_cb);
        MatterManager::instance().on_device_added(*cfg);
    }

    return ESP_OK;
}

void DeviceManager::start_polling()
{
    ModbusManager::instance().start_polling();
}

esp_err_t DeviceManager::register_device(const device_config_t &config)
{
    ESP_LOGI(TAG, "Registering device '%s' with Matter", config.name);

    ESP_LOGI(TAG, "Adding device to ModbusManager");
    ModbusManager::instance().on_device_added(config, readings_dispatch_cb);

    ESP_LOGI(TAG, "Adding device to MatterManager");
    esp_err_t err = MatterManager::instance().on_device_added(config);

    if (err == ESP_OK) {
        ModbusDevice *modbus = ModbusManager::instance().find(config.id);
        modbus->start_polling();
    } else {
        ModbusManager::instance().on_device_removed(config.id);
        ESP_LOGE(TAG, "Failed to register Matter device for '%s', rolling back", config.id);
    }

    return err;
}

void DeviceManager::unregister_device(const char *id)
{
    MatterManager::instance().on_device_removed(id);
    ModbusManager::instance().on_device_removed(id);
}

esp_err_t DeviceManager::add_device(const char *name, const char *host,
                                    uint16_t port, uint8_t unit_id,
                                    const char *matter_structure_json,
                                    device_config_t *out)
{
    ESP_LOGI(TAG, "Adding device: name=%s, host=%s, port=%u, unitId=%u", name, host, port, unit_id);

    device_config_t *created = new device_config_t();
    if (!created) return ESP_ERR_NO_MEM;

    esp_err_t err = devices_store_add(name, host, port, unit_id, matter_structure_json, created);
    if (err != ESP_OK) { delete created; return err; }

    ESP_LOGI(TAG, "Device '%s' stored with id '%s'", name, created->id);

    err = register_device(*created);
    if (err != ESP_OK) { delete created; return err; }

    // Re-read from store so `out` includes the endpoint ID written back by MatterManager.
    const device_config_t *updated = devices_store_find(created->id);
    if (out) *out = updated ? *updated : *created;
    delete created;
    return ESP_OK;
}

esp_err_t DeviceManager::update_device(const char *id, const char *name, const char *host,
                                       uint16_t port, uint8_t unit_id,
                                       const char *matter_structure_json,
                                       device_config_t *out)
{
    device_config_t *updated = new device_config_t();
    if (!updated) return ESP_ERR_NO_MEM;

    esp_err_t err = devices_store_update(id, name, host, port, unit_id,
                                         matter_structure_json, updated);
    if (err != ESP_OK) { delete updated; return err; }

    err = register_device(*updated);
    delete updated;
    return err;
}

esp_err_t DeviceManager::remove_device(const char *id)
{
    unregister_device(id);
    return devices_store_remove(id);
}

void DeviceManager::clear()
{
    MatterManager::instance().clear();
    ModbusManager::instance().clear();
    devices_store_clear();
}

uint16_t DeviceManager::endpoint_id(const char *id) const
{
    return MatterManager::instance().endpoint_id(id);
}

bool DeviceManager::get_readings(const char *id, DeviceReadings &out) const
{
    return MatterManager::instance().get_readings(id, out);
}
