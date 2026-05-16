#include "device_manager.h"
#include "modbus_manager.h"
#include "matter_manager.h"

#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "device_manager";

static void readings_dispatch_cb(const std::vector<RegisterReading> &readings, void *arg)
{
    MatterManager::instance().on_readings(static_cast<const char *>(arg), readings);
}

static std::vector<RegisterSpec> build_register_specs(const char *matter_structure_json)
{
    std::vector<RegisterSpec> regs;
    if (!matter_structure_json || matter_structure_json[0] == '\0') return regs;

    cJSON *root = cJSON_Parse(matter_structure_json);
    if (!root) return regs;

    cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    if (cJSON_IsArray(endpoints)) {
        cJSON *ep = nullptr;
        cJSON_ArrayForEach(ep, endpoints) {
            cJSON *mappings = cJSON_GetObjectItemCaseSensitive(ep, "mappings");
            if (!cJSON_IsArray(mappings)) continue;
            cJSON *m = nullptr;
            cJSON_ArrayForEach(m, mappings) {
                cJSON *func = cJSON_GetObjectItemCaseSensitive(m, "function");
                cJSON *addr = cJSON_GetObjectItemCaseSensitive(m, "address");
                if (!cJSON_IsNumber(func) || !cJSON_IsNumber(addr)) continue;
                regs.push_back({(uint16_t)addr->valueint, func->valueint == 4});
            }
        }
    }
    cJSON_Delete(root);
    return regs;
}

DeviceManager &DeviceManager::instance()
{
    static DeviceManager s_instance;
    return s_instance;
}

esp_err_t DeviceManager::init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator)
{
    ModbusManager::instance().init();

    size_t count = devices_store_count();
    for (size_t i = 0; i < count; i++) {
        const device_config_t *cfg = devices_store_at(i);
        auto regs = build_register_specs(cfg->matter_structure_json);
        ModbusManager::instance().on_device_added(*cfg, regs, readings_dispatch_cb);
    }
    return MatterManager::instance().init(node, aggregator);
}

void DeviceManager::start_polling()
{
    ModbusManager::instance().start_polling();
}

esp_err_t DeviceManager::register_device(const device_config_t &config)
{
    ESP_LOGI(TAG, "Registering device '%s' with Matter", config.name);

    auto regs = build_register_specs(config.matter_structure_json);

    ESP_LOGI(TAG, "Registering registers to read");

    for (auto i: regs) {
        ESP_LOGI(TAG, "  - %s register at address %u", i.input ? "Input" : "Holding", i.address);
    }

    ESP_LOGI(TAG, "Adding device to ModbusManager");
    ModbusManager::instance().on_device_added(config, regs, readings_dispatch_cb);

    ModbusDevice *modbus = ModbusManager::instance().find(config.id);

    ESP_LOGI(TAG, "Adding device to MatterManager");
    esp_err_t err = MatterManager::instance().on_device_added(config, modbus);

    if (err == ESP_OK) {
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
    ESP_LOGI(TAG, "Adding Modbus device: name=%s, host=%s, port=%u, unitId=%u", name, host, port, unit_id);

    device_config_t created;
    esp_err_t err = devices_store_add(name, host, port, unit_id, matter_structure_json, &created);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Device '%s' stored with id '%s'", name, created.id);

    err = register_device(created);
    if (err != ESP_OK) return err;

    // Re-read from store so `out` includes the endpoint ID written back by MatterManager.
    const device_config_t *updated = devices_store_find(created.id);
    if (out) *out = updated ? *updated : created;
    return ESP_OK;
}

esp_err_t DeviceManager::update_device(const char *id, const char *name, const char *host,
                                       uint16_t port, uint8_t unit_id,
                                       const char *matter_structure_json,
                                       device_config_t *out)
{
    device_config_t updated;
    esp_err_t err = devices_store_update(id, name, host, port, unit_id,
                                         matter_structure_json, &updated);
    if (err != ESP_OK) return err;
    return register_device(updated);
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
