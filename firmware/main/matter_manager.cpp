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
#include <clusters/PowerSource/ClusterId.h>
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

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static void parse_endpoint_mappings(cJSON *ep_json, size_t endpoint_idx,
                                    std::vector<MatterManager::AttributeMapping> &out)
{
    cJSON *ms = cJSON_GetObjectItemCaseSensitive(ep_json, "mappings");
    if (!cJSON_IsArray(ms))
        return;
    cJSON *m = nullptr;
    cJSON_ArrayForEach(m, ms)
    {
        cJSON *addr = cJSON_GetObjectItemCaseSensitive(m, "address");
        cJSON *cluster = cJSON_GetObjectItemCaseSensitive(m, "cluster");
        cJSON *attr = cJSON_GetObjectItemCaseSensitive(m, "attribute");
        if (cJSON_IsNumber(addr) && cJSON_IsNumber(cluster) && cJSON_IsNumber(attr))
        {
            out.push_back({(uint16_t)addr->valueint,
                           (uint32_t)cluster->valueint,
                           (uint32_t)attr->valueint,
                           endpoint_idx});
        }
    }
}

std::vector<MatterManager::AttributeMapping>
MatterManager::parse_all_mappings(const char *json)
{
    std::vector<AttributeMapping> out;
    if (!json || !json[0])
        return out;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return out;

    cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(endpoints) ? cJSON_GetArrayItem(endpoints, 0) : nullptr;
    if (ep0)
    {
        parse_endpoint_mappings(ep0, 0, out);

        cJSON *parts = cJSON_GetObjectItemCaseSensitive(ep0, "parts");
        if (cJSON_IsArray(parts))
        {
            int n = cJSON_GetArraySize(parts);
            for (int i = 0; i < n; i++)
            {
                cJSON *part = cJSON_GetArrayItem(parts, i);
                if (part)
                    parse_endpoint_mappings(part, (size_t)(i + 1), out);
            }
        }
    }
    cJSON_Delete(root);
    return out;
}

static uint16_t get_stored_root_ep_id(const char *json)
{
    if (!json || !json[0])
        return MATTER_ENDPOINT_ID_INVALID;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return MATTER_ENDPOINT_ID_INVALID;
    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(eps) ? cJSON_GetArrayItem(eps, 0) : nullptr;
    uint16_t result = MATTER_ENDPOINT_ID_INVALID;
    if (ep0)
    {
        cJSON *ep_id = cJSON_GetObjectItemCaseSensitive(ep0, "endpointId");
        if (cJSON_IsNumber(ep_id))
            result = (uint16_t)ep_id->valueint;
    }
    cJSON_Delete(root);
    return result;
}

static uint16_t get_stored_part_ep_id(const char *json, int part_index)
{
    if (!json || !json[0])
        return MATTER_ENDPOINT_ID_INVALID;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return MATTER_ENDPOINT_ID_INVALID;
    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(eps) ? cJSON_GetArrayItem(eps, 0) : nullptr;
    uint16_t result = MATTER_ENDPOINT_ID_INVALID;
    if (ep0)
    {
        cJSON *parts = cJSON_GetObjectItemCaseSensitive(ep0, "parts");
        cJSON *part = cJSON_IsArray(parts) ? cJSON_GetArrayItem(parts, part_index) : nullptr;
        if (part)
        {
            cJSON *ep_id = cJSON_GetObjectItemCaseSensitive(part, "endpointId");
            if (cJSON_IsNumber(ep_id))
                result = (uint16_t)ep_id->valueint;
        }
    }
    cJSON_Delete(root);
    return result;
}

static esp_err_t write_all_endpoint_ids(const char *json_in,
                                        uint16_t root_ep_id,
                                        const std::vector<uint16_t> &part_ep_ids,
                                        char *json_out, size_t json_out_sz)
{
    cJSON *root = cJSON_Parse(json_in);
    if (!root)
        return ESP_FAIL;

    cJSON *eps = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    cJSON *ep0 = cJSON_IsArray(eps) ? cJSON_GetArrayItem(eps, 0) : nullptr;
    if (ep0)
    {
        cJSON_DeleteItemFromObjectCaseSensitive(ep0, "endpointId");
        cJSON_AddNumberToObject(ep0, "endpointId", root_ep_id);

        cJSON *parts = cJSON_GetObjectItemCaseSensitive(ep0, "parts");
        if (cJSON_IsArray(parts))
        {
            for (size_t i = 0; i < part_ep_ids.size(); i++)
            {
                cJSON *part = cJSON_GetArrayItem(parts, (int)i);
                if (part)
                {
                    cJSON_DeleteItemFromObjectCaseSensitive(part, "endpointId");
                    cJSON_AddNumberToObject(part, "endpointId", part_ep_ids[i]);
                }
            }
        }
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text)
        return ESP_ERR_NO_MEM;
    strlcpy(json_out, text, json_out_sz);
    free(text);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Bridge callbacks
// ---------------------------------------------------------------------------

// Add the device-type clusters for `device_type_id` to an existing endpoint. Shared by the bridge
// callback (root/bridged endpoints) and by composed part endpoints created directly.
static esp_err_t add_device_clusters(esp_matter::endpoint_t *ep, uint32_t device_type_id)
{
    ESP_LOGI(TAG, "Adding device type 0x%08" PRIx32, device_type_id);

    if (device_type_id == ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID)
    {
        solar_power::config_t cfg;
        if (solar_power::add(ep, &cfg) != ESP_OK)
        {
            ESP_LOGE(TAG, "solar_power::add failed");
            return ESP_FAIL;
        }
    }
    else if (device_type_id == ESP_MATTER_POWER_SOURCE_DEVICE_TYPE_ID)
    {
        power_source_device::config_t cfg;
        cfg.power_source.feature_flags = power_source::feature::battery::get_id(); // TODO Assume battery for now.

        if (power_source_device::add(ep, &cfg) != ESP_OK)
        {
            ESP_LOGE(TAG, "power_source_device::add failed");
            return ESP_FAIL;
        }

        // BatPercentRemaining (state of charge) is an optional battery attribute that
        // power_source_device::add does not create, so add it explicitly (0-200 half-percent).
        cluster_t *ps = cluster::get(ep, chip::app::Clusters::PowerSource::Id);
        if (ps)
            power_source::attribute::create_bat_percent_remaining(ps, nullable<uint8_t>(), 0, 200);
    }
    else if (device_type_id == ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID)
    {
        electrical_sensor::config_t cfg;

        // The feature flag configuration needs to come from the JSON!
        cfg.electrical_power_measurement.feature_flags |= electrical_power_measurement::feature::alternating_current::get_id();

        if (electrical_sensor::add(ep, &cfg) != ESP_OK)
        {
            ESP_LOGE(TAG, "electrical_sensor::add failed");
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGW(TAG, "No cluster setup for device type 0x%08" PRIx32, device_type_id);
    }
    return ESP_OK;
}

esp_err_t MatterManager::device_type_callback(esp_matter::endpoint_t *ep,
                                              uint32_t device_type_id,
                                              void *priv_data)
{
    ESP_LOGI(TAG, "Creating bridged device for type 0x%08" PRIx32, device_type_id);

    if (add_device_clusters(ep, device_type_id) != ESP_OK)
        return ESP_FAIL;

    // Set the node label to the device name for easier identification of the bridged devices.
    //
    const device_config_t *dev_cfg = static_cast<const device_config_t *>(priv_data);

    if (dev_cfg)
    {
        cluster_t *bdbi = cluster::get(ep, chip::app::Clusters::BridgedDeviceBasicInformation::Id);
        if (bdbi)
        {
            bridged_device_basic_information::attribute::create_node_label(
                bdbi, (char *)dev_cfg->name, strlen(dev_cfg->name));
        }
    }

    return ESP_OK;
}

// Instantiate the concrete IMatterDevice (which owns the cluster delegates) that matches the
// endpoint's Matter device type. Keep this in sync with add_device_clusters above.
static IMatterDevice *make_matter_dev(uint32_t device_type_id,
                                      esp_matter::endpoint_t *ep,
                                      const device_config_t &config)
{
    switch (device_type_id)
    {
    case ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID:
        return new SolarPowerDevice(ep, config);
    case ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID:
    default:
        // ElectricalSensorDevice handles the EPM cluster (voltage/current/power) and, when the
        // endpoint is also composed with a Power Source device type, the PowerSource state of charge.
        return new ElectricalSensorDevice(ep, config);
    }
}

// ---------------------------------------------------------------------------
// Core device management
// ---------------------------------------------------------------------------

MatterManager::EndpointEntry MatterManager::create_or_resume_endpoint(
    uint32_t device_type_id, uint16_t parent_ep_id,
    uint16_t stored_ep_id, const device_config_t &config,
    bool &newly_created_out, uint16_t &new_ep_id_out)
{
    EndpointEntry entry;
    newly_created_out = false;
    new_ep_id_out = MATTER_ENDPOINT_ID_INVALID;

    // Our device has an endpointId, meaning it's part of the bridge already.
    //
    if (stored_ep_id != MATTER_ENDPOINT_ID_INVALID)
    {
        entry.bridge_dev = esp_matter_bridge::resume_device(
            m_node, stored_ep_id, const_cast<device_config_t *>(&config));
        if (!entry.bridge_dev)
        {
            ESP_LOGW(TAG, "[%s] resume ep=%u failed, recreating", config.id, stored_ep_id);
        }
    }

    // We have no bridged device yet, so we need to create a new one.
    //
    if (!entry.bridge_dev)
    {
        entry.bridge_dev = esp_matter_bridge::create_device(
            m_node, parent_ep_id, device_type_id,
            const_cast<device_config_t *>(&config));
        if (!entry.bridge_dev)
        {
            ESP_LOGE(TAG, "[%s] create_device failed (parent_ep=%u type=0x%08" PRIx32 ")",
                     config.id, parent_ep_id, device_type_id);
            return entry;
        }
        newly_created_out = true;
        new_ep_id_out = endpoint::get_id(entry.bridge_dev->endpoint);
    }

    if (entry.bridge_dev)
        entry.endpoint = entry.bridge_dev->endpoint;
    return entry;
}

MatterManager::EndpointEntry MatterManager::create_or_resume_part(
    const std::vector<uint32_t> &device_types, esp_matter::endpoint_t *parent_ep,
    uint16_t stored_ep_id, const device_config_t &config,
    bool &newly_created_out, uint16_t &new_ep_id_out)
{
    EndpointEntry entry;
    newly_created_out = false;
    new_ep_id_out = MATTER_ENDPOINT_ID_INVALID;

    void *priv = const_cast<device_config_t *>(&config);
    uint32_t primary_type = device_types.empty() ? ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID
                                                 : device_types.front();

    // Parts are plain composed endpoints (no BridgedDeviceBasicInformation). The bridge API only
    // permits aggregator-parented devices, so we build them the same way common::create does and
    // then parent them to the root endpoint. resume() re-uses the stored id so part endpoint ids
    // stay stable across reboots.
    if (stored_ep_id != MATTER_ENDPOINT_ID_INVALID)
    {
        entry.endpoint = esp_matter::endpoint::resume(m_node, ENDPOINT_FLAG_DESTROYABLE, stored_ep_id, priv);
        if (!entry.endpoint)
            ESP_LOGW(TAG, "[%s] resume part ep=%u failed, recreating", config.id, stored_ep_id);
    }

    if (!entry.endpoint)
    {
        entry.endpoint = esp_matter::endpoint::create(m_node, ENDPOINT_FLAG_DESTROYABLE, priv);
        if (!entry.endpoint)
        {
            ESP_LOGE(TAG, "[%s] endpoint::create failed for part type=0x%08" PRIx32, config.id, primary_type);
            return entry;
        }
        newly_created_out = true;
        new_ep_id_out = endpoint::get_id(entry.endpoint);
    }

    // Mirror common::create: descriptor cluster, then the device-type clusters. A part may declare
    // multiple device types (e.g. Electrical Sensor + Power Source for a battery), so add the
    // clusters for every declared type onto this one composed endpoint.
    descriptor::config_t desc_cfg;
    if (!descriptor::create(entry.endpoint, &desc_cfg, CLUSTER_FLAG_SERVER))
    {
        ESP_LOGE(TAG, "[%s] descriptor::create failed for part", config.id);
    }
    for (uint32_t dt : device_types)
        add_device_clusters(entry.endpoint, dt);

    // Compose under the root endpoint (adds this endpoint to the root's Descriptor PartsList).
    if (parent_ep)
        esp_matter::endpoint::set_parent_endpoint(entry.endpoint, parent_ep);

    return entry;
}

esp_err_t MatterManager::create_matter_device(const device_config_t &config)
{
    uint32_t root_device_type = -1;
    int part_count = 0;

    // All device types declared for each part (a part may compose several, e.g. Electrical Sensor
    // + Power Source for a battery). Index 0 is the primary type used to pick the device wrapper.
    std::vector<std::vector<uint32_t>> part_device_types;

    {
        cJSON *js = cJSON_Parse(config.matter_structure_json);
        if (js)
        {
            cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(js, "endpoints");
            cJSON *ep0 = cJSON_IsArray(endpoints) ? cJSON_GetArrayItem(endpoints, 0) : nullptr;
            if (ep0)
            {
                cJSON *dts = cJSON_GetObjectItemCaseSensitive(ep0, "deviceTypes");
                cJSON *dt0 = cJSON_IsArray(dts) ? cJSON_GetArrayItem(dts, 0) : nullptr;
                if (dt0 && cJSON_IsNumber(dt0))
                    root_device_type = (uint32_t)dt0->valueint;

                cJSON *parts = cJSON_GetObjectItemCaseSensitive(ep0, "parts");
                if (cJSON_IsArray(parts))
                {
                    part_count = cJSON_GetArraySize(parts);
                    for (int i = 0; i < part_count; i++)
                    {
                        cJSON *part = cJSON_GetArrayItem(parts, i);
                        std::vector<uint32_t> types;
                        cJSON *pdts = cJSON_GetObjectItemCaseSensitive(part, "deviceTypes");
                        if (cJSON_IsArray(pdts))
                        {
                            cJSON *t = nullptr;
                            cJSON_ArrayForEach(t, pdts)
                            {
                                if (cJSON_IsNumber(t))
                                    types.push_back((uint32_t)t->valueint);
                            }
                        }
                        if (types.empty())
                            types.push_back(ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID);
                        part_device_types.push_back(std::move(types));
                    }
                }
            }
            cJSON_Delete(js);
        }
    }

    auto all_mappings = parse_all_mappings(config.matter_structure_json);

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    // --- Root endpoint ---
    bool root_new = false;
    uint16_t root_new_id = MATTER_ENDPOINT_ID_INVALID;
    uint16_t stored_root = get_stored_root_ep_id(config.matter_structure_json);
    EndpointEntry root_entry = create_or_resume_endpoint(
        root_device_type, m_aggregator_endpoint_id,
        stored_root, config, root_new, root_new_id);

    if (!root_entry.bridge_dev)
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return ESP_FAIL;
    }

    uint16_t root_ep_id = endpoint::get_id(root_entry.bridge_dev->endpoint);

    // --- Part endpoints (parented to root) ---
    std::vector<EndpointEntry> part_entries;
    std::vector<bool> part_new_flags(part_count, false);
    std::vector<uint16_t> part_new_ids(part_count, MATTER_ENDPOINT_ID_INVALID);

    for (int i = 0; i < part_count; i++)
    {
        uint16_t stored_part = get_stored_part_ep_id(config.matter_structure_json, i);
        bool pnew = false;
        uint16_t pnew_id = MATTER_ENDPOINT_ID_INVALID;
        EndpointEntry pe = create_or_resume_part(
            part_device_types[i], root_entry.endpoint,
            stored_part, config, pnew, pnew_id);
        if (!pe.endpoint)
        {
            ESP_LOGE(TAG, "[%s] failed to create part %d", config.id, i);
        }
        part_entries.push_back(pe);
        part_new_flags[i] = pnew;
        part_new_ids[i] = pnew_id;
    }

    // Enable root, then all parts
    endpoint::enable(root_entry.endpoint);
    for (auto &pe : part_entries)
    {
        if (pe.endpoint)
            endpoint::enable(pe.endpoint);
    }

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    // --- Persist new endpoint IDs (outside CHIP lock) ---
    bool any_new = root_new;
    for (int i = 0; i < part_count; i++)
        any_new |= part_new_flags[i];

    if (any_new)
    {
        uint16_t persist_root_id = root_new ? root_new_id : stored_root;
        std::vector<uint16_t> persist_part_ids;
        for (int i = 0; i < part_count; i++)
        {
            persist_part_ids.push_back(
                part_new_flags[i] ? part_new_ids[i]
                                  : get_stored_part_ep_id(config.matter_structure_json, i));
        }

        char *updated_json = (char *)malloc(MATTER_STRUCTURE_JSON_LEN);
        if (updated_json)
        {
            if (write_all_endpoint_ids(config.matter_structure_json,
                                       persist_root_id,
                                       persist_part_ids,
                                       updated_json,
                                       MATTER_STRUCTURE_JSON_LEN) == ESP_OK)
            {
                devices_store_update_matter_structure(config.id, updated_json);
            }
            free(updated_json);
        }
    }

    // Create our internal devices that contain the Matter delegates etc., dispatching on the
    // device type declared in the matter_structure for each endpoint.
    //
    root_entry.matter_dev = make_matter_dev(root_device_type, root_entry.endpoint, config);
    for (size_t i = 0; i < part_entries.size(); i++)
    {
        if (part_entries[i].endpoint)
            part_entries[i].matter_dev = make_matter_dev(part_device_types[i].front(), part_entries[i].endpoint, config);
    }

    // We need to store all the endpoints and mappings together so we can route readings updates properly.
    //
    BridgedMatterDevice bridge_matter_device;
    strncpy(bridge_matter_device.id, config.id, sizeof(bridge_matter_device.id) - 1);
    
    bridge_matter_device.id[sizeof(bridge_matter_device.id) - 1] = '\0';
    bridge_matter_device.endpoints.push_back(root_entry);

    for (auto &pe : part_entries)
        bridge_matter_device.endpoints.push_back(pe);
    bridge_matter_device.mappings = all_mappings;
    m_devices.push_back(std::move(bridge_matter_device));

    ESP_LOGI(TAG, "Registered '%s': root ep=%u + %d part(s)", config.name, root_ep_id, part_count);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

esp_err_t MatterManager::init(esp_matter::node_t *node, esp_matter::endpoint_t *aggregator)
{
    m_node = node;
    m_aggregator_endpoint_id = endpoint::get_id(aggregator);

    devices_store_init();

    ESP_LOGI(TAG, "Initializing the esp_matter_bridge...");

    esp_err_t err = esp_matter_bridge::initialize(node, device_type_callback);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_matter_bridge::initialize failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "MatterBridge initialized");

    return ESP_OK;
}

esp_err_t MatterManager::on_device_added(const device_config_t &config)
{
    return create_matter_device(config);
}

void MatterManager::on_readings(const char *id, const std::vector<RegisterReading> &readings)
{
    ESP_LOGI(TAG, "Received %zu readings for device '%s'", readings.size(), id);

    auto it = std::find_if(m_devices.begin(), m_devices.end(), [id](const BridgedMatterDevice &p)
                           { return strcmp(p.id, id) == 0; });

    if (it == m_devices.end())
    {
        ESP_LOGW(TAG, "No Matter device found to receive the readings from id '%s'", id);
        return;
    }

    // We now need to find all the endpoints that are interested in these readings.
    //
    for (const auto &r : readings)
    {
        for (const auto &m : it->mappings)
        {
            if (m.reg_address == r.address && m.endpoint_idx < it->endpoints.size())
            {
                IMatterDevice *dev = it->endpoints[m.endpoint_idx].matter_dev;
                if (dev)
                    dev->set_raw(m.cluster_id, m.attribute_id, r.value);
            }
        }
    }
}

bool MatterManager::get_readings(const char *id, DeviceReadings &out) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
                           [id](const BridgedMatterDevice &p)
                           { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end())
        return false;

    for (const auto &m : it->mappings)
    {
        if (m.endpoint_idx >= it->endpoints.size())
            continue;
        const EndpointEntry &entry = it->endpoints[m.endpoint_idx];
        if (!entry.matter_dev)
            continue;
        int64_t value = 0;
        if (entry.matter_dev->get_raw(m.cluster_id, m.attribute_id, value)) {
            out.push_back({entry.matter_dev->endpoint_id(), m.cluster_id, m.attribute_id, value});
        }
    }
    return true;
}

esp_err_t MatterManager::on_device_removed(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
                           [id](const BridgedMatterDevice &p)
                           { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end())
        return ESP_ERR_NOT_FOUND;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    // Destroy parts (children) before the root so the parent's PartsList stays consistent.
    for (auto rit = it->endpoints.rbegin(); rit != it->endpoints.rend(); ++rit)
    {
        delete rit->matter_dev;
        if (rit->bridge_dev)
            esp_matter_bridge::remove_device(rit->bridge_dev);
        else if (rit->endpoint)
            esp_matter::endpoint::destroy(m_node, rit->endpoint);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    m_devices.erase(it);
    ESP_LOGI(TAG, "Matter device removed: %s", id);
    return ESP_OK;
}

void MatterManager::clear()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    for (auto &pair : m_devices)
    {
        // Destroy parts (children) before the root so the parent's PartsList stays consistent.
        for (auto rit = pair.endpoints.rbegin(); rit != pair.endpoints.rend(); ++rit)
        {
            delete rit->matter_dev;
            if (rit->bridge_dev)
                esp_matter_bridge::remove_device(rit->bridge_dev);
            else if (rit->endpoint)
                esp_matter::endpoint::destroy(m_node, rit->endpoint);
        }
    }
    esp_matter_bridge::factory_reset();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    m_devices.clear();
    ESP_LOGI(TAG, "All Matter devices cleared");
}

uint16_t MatterManager::endpoint_id(const char *id) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
                           [id](const BridgedMatterDevice &p)
                           { return strcmp(p.id, id) == 0; });
    if (it == m_devices.end() || it->endpoints.empty() || !it->endpoints[0].matter_dev)
        return 0;
    return it->endpoints[0].matter_dev->endpoint_id();
}