#include "matter_manager.h"
#include "modbus_manager.h"

#include <algorithm>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"
#include <esp_matter_bridge.h>
#include <esp_matter_endpoint.h>
#include <platform/CHIPDeviceLayer.h>

#include <clusters/BridgedDeviceBasicInformation/ClusterId.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;

static const char *TAG = "matter_manager";

MatterManager &MatterManager::instance()
{
    static MatterManager s_instance;
    return s_instance;
}


std::vector<MatterManager::AttributeMapping> MatterManager::parse_mappings(const char *matter_structure_json)
{
    std::vector<AttributeMapping> mappings;
    if (!matter_structure_json || matter_structure_json[0] == '\0') return mappings;

    cJSON *root = cJSON_Parse(matter_structure_json);
    if (!root) return mappings;

    cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(endpoints) ? cJSON_GetArrayItem(endpoints, 0) : nullptr;
    if (ep0) {
        cJSON *ms = cJSON_GetObjectItemCaseSensitive(ep0, "mappings");
        if (cJSON_IsArray(ms)) {
            cJSON *m = nullptr;
            cJSON_ArrayForEach(m, ms) {
                cJSON *addr    = cJSON_GetObjectItemCaseSensitive(m, "address");
                cJSON *cluster = cJSON_GetObjectItemCaseSensitive(m, "cluster");
                cJSON *attr    = cJSON_GetObjectItemCaseSensitive(m, "attribute");
                if (cJSON_IsNumber(addr) && cJSON_IsNumber(cluster) && cJSON_IsNumber(attr)) {
                    mappings.push_back({
                        (uint16_t)addr->valueint,
                        (uint32_t)cluster->valueint,
                        (uint32_t)attr->valueint
                    });
                }
            }
        }
    }
    cJSON_Delete(root);
    return mappings;
}

static uint16_t get_stored_endpoint_id(const char *matter_structure_json)
{
    if (!matter_structure_json || matter_structure_json[0] == '\0')
        return MATTER_ENDPOINT_ID_INVALID;
    cJSON *root = cJSON_Parse(matter_structure_json);
    if (!root) return MATTER_ENDPOINT_ID_INVALID;
    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(eps) ? cJSON_GetArrayItem(eps, 0) : nullptr;
    uint16_t result = MATTER_ENDPOINT_ID_INVALID;
    if (ep0) {
        cJSON *ep_id = cJSON_GetObjectItemCaseSensitive(ep0, "endpointId");
        if (cJSON_IsNumber(ep_id)) result = (uint16_t)ep_id->valueint;
    }
    cJSON_Delete(root);
    return result;
}

static esp_err_t write_endpoint_id_to_matter_structure(const char *json_in,
                                                        uint16_t ep_id,
                                                        char *json_out,
                                                        size_t json_out_sz)
{
    cJSON *root = cJSON_Parse(json_in);
    if (!root) return ESP_FAIL;
    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(eps) ? cJSON_GetArrayItem(eps, 0) : nullptr;
    if (ep0) {
        cJSON_DeleteItemFromObjectCaseSensitive(ep0, "endpointId");
        cJSON_AddNumberToObject(ep0, "endpointId", ep_id);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return ESP_ERR_NO_MEM;
    strlcpy(json_out, text, json_out_sz);
    free(text);
    return ESP_OK;
}

void MatterManager::on_readings(const char *id, const std::vector<RegisterReading> &readings)
{
    ESP_LOGI(TAG, "ModBus readings received for device '%s'", id);

    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end()) return;

    for (const auto &r : readings) {
        for (const auto &m : it->mappings) {
            if (m.reg_address == r.address) {
                it->matter->set_raw(m.cluster_id, m.attribute_id, r.value);
                break;
            }
        }
    }
}

esp_err_t MatterManager::device_type_callback(esp_matter::endpoint_t *ep,
                                               uint32_t device_type_id,
                                               void *priv_data)
{
    if (device_type_id == ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID) {
        solar_power::config_t sp_cfg;
        if (solar_power::add(ep, &sp_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "solar_power::add failed");
            return ESP_FAIL;
        }
        const device_config_t *cfg = static_cast<const device_config_t *>(priv_data);
        if (cfg) {
            cluster_t *bdbi = cluster::get(ep, chip::app::Clusters::BridgedDeviceBasicInformation::Id);
            if (bdbi) {
                bridged_device_basic_information::attribute::create_node_label(bdbi, (char *)cfg->name, strlen(cfg->name));
            }
        }
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Unsupported device type: 0x%08" PRIx32, device_type_id);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t MatterManager::init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator)
{
    m_node                   = node;
    m_aggregator_endpoint_id = endpoint::get_id(aggregator);

    esp_err_t err = esp_matter_bridge::initialize(node, device_type_callback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter_bridge::initialize failed: %d", err);
        return err;
    }

    size_t count = devices_store_count();
    for (size_t i = 0; i < count; i++) {
        const device_config_t *cfg = devices_store_at(i);
        ModbusDevice *modbus = ModbusManager::instance().find(cfg->id);
        if (!modbus) {
            ESP_LOGW(TAG, "No Modbus device found for '%s', skipping Matter registration", cfg->id);
            continue;
        }
        if (create_matter_device(*cfg, modbus) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Matter device '%s'", cfg->id);
        }
    }

    return ESP_OK;
}

esp_err_t MatterManager::create_matter_device(const device_config_t &config, ModbusDevice *modbus)
{
    // Determine primary device type from matter_structure
    uint32_t primary_device_type = ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID;
    cJSON *root = cJSON_Parse(config.matter_structure_json);
    if (root) {
        cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
        cJSON *ep0 = cJSON_IsArray(endpoints) ? cJSON_GetArrayItem(endpoints, 0) : nullptr;
        if (ep0) {
            cJSON *device_types = cJSON_GetObjectItemCaseSensitive(ep0, "deviceTypes");
            cJSON *first_type   = cJSON_IsArray(device_types)
                                      ? cJSON_GetArrayItem(device_types, 0)
                                      : nullptr;
            if (first_type && cJSON_IsNumber(first_type)) {
                primary_device_type = (uint32_t)first_type->valueint;
            }
        }
        cJSON_Delete(root);
    }

    auto mappings = parse_mappings(config.matter_structure_json);

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    esp_matter_bridge::device_t *bridge_dev = nullptr;
    bool newly_created = false;
    uint16_t new_ep_id = MATTER_ENDPOINT_ID_INVALID;

    uint16_t stored_ep_id = get_stored_endpoint_id(config.matter_structure_json);
    if (stored_ep_id != MATTER_ENDPOINT_ID_INVALID) {
        bridge_dev = esp_matter_bridge::resume_device(m_node, stored_ep_id,
                                                      const_cast<device_config_t *>(&config));
        if (!bridge_dev) {
            ESP_LOGW(TAG, "[%s] resume ep=%u failed, recreating", config.id, stored_ep_id);
        }
    }

    // If we don't have this Matter devices in the Bridge already, create it.
    if (!bridge_dev) {
        bridge_dev = esp_matter_bridge::create_device(m_node,
                                                      m_aggregator_endpoint_id,
                                                      primary_device_type,
                                                      const_cast<device_config_t *>(&config));
        if (!bridge_dev) {
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
            ESP_LOGE(TAG, "[%s] create Matter device failed", config.id);
            return ESP_FAIL;
        }
        newly_created = true;
        new_ep_id = endpoint::get_id(bridge_dev->endpoint);
    }

    endpoint::enable(bridge_dev->endpoint);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    // Flash write must happen outside the CHIP stack lock — holding it while
    // writing NVS/LittleFS causes SPI flash mutex priority inversion and a crash.
    if (newly_created) {
        char updated_json[MATTER_STRUCTURE_JSON_LEN];
        if (write_endpoint_id_to_matter_structure(config.matter_structure_json,
                                                   new_ep_id,
                                                   updated_json,
                                                   sizeof(updated_json)) == ESP_OK) {
            devices_store_update_matter_structure(config.id, updated_json);
        }
    }

    auto *solar = new SolarPowerDevice(bridge_dev, config);

    DevicePair pair;
    strncpy(pair.id, config.id, sizeof(pair.id) - 1);
    pair.id[sizeof(pair.id) - 1] = '\0';
    pair.matter     = solar;
    pair.bridge_dev = bridge_dev;
    pair.mappings   = mappings;
    m_devices.push_back(pair);

    ESP_LOGI(TAG, "Registered '%s' -> ep=%u", config.name, endpoint::get_id(bridge_dev->endpoint));
    return ESP_OK;
}

esp_err_t MatterManager::on_device_added(const device_config_t &config, ModbusDevice *modbus)
{
    return create_matter_device(config, modbus);
}

esp_err_t MatterManager::on_device_removed(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end()) return ESP_ERR_NOT_FOUND;

    delete it->matter;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_matter_bridge::remove_device(it->bridge_dev);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    m_devices.erase(it);
    ESP_LOGI(TAG, "Matter device removed: %s", id);
    return ESP_OK;
}

void MatterManager::clear()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    for (auto &pair : m_devices) {
        delete pair.matter;
        esp_matter_bridge::remove_device(pair.bridge_dev);
    }
    esp_matter_bridge::factory_reset();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    m_devices.clear();
    ESP_LOGI(TAG, "All Matter devices cleared");
}

uint16_t MatterManager::endpoint_id(const char *id) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.id, id) == 0; });
    return it != m_devices.end() ? it->matter->endpoint_id() : 0;
}

bool MatterManager::get_readings(const char *id, DeviceReadings &out) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](const DevicePair &p) { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end()) return false;

    uint16_t ep_id = it->matter->endpoint_id();
    for (const auto &m : it->mappings) {
        int64_t value = 0;
        if (it->matter->get_raw(m.cluster_id, m.attribute_id, value)) {
            out.push_back({ep_id, m.cluster_id, m.attribute_id, value});
        }
    }
    return true;
}
