#include "modbus_manager.h"
#include "electrical_sensor_device.h"

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
    auto *a = static_cast<ModbusManager::ReadingsArg *>(arg);
    a->solar->set_voltage(r.voltage_raw);
    a->solar->set_active_current(r.current_raw);
    a->solar->set_active_power(r.power_raw);
    // a->pv1->set_voltage(r.pv1_voltage_raw);
    // a->pv1->set_active_current(r.pv1_current_raw);
    // a->pv2->set_voltage(r.pv2_voltage_raw);
    // a->pv2->set_active_current(r.pv2_current_raw);
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
                bridged_device_basic_information::attribute::create_node_label(
                    bdbi, (char *)cfg->name, strlen(cfg->name));
            }
        }

        ESP_LOGE(TAG, "Added Solar Power device");

        return ESP_OK;
    }

    // if (device_type_id == ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID) {
    //     electrical_sensor::config_t es_cfg;
    //     es_cfg.electrical_power_measurement.feature_flags =
    //         electrical_power_measurement::feature::direct_current::get_id();
    //     if (electrical_sensor::add(ep, &es_cfg) != ESP_OK) {
    //         ESP_LOGE(TAG, "electrical_sensor::add failed");
    //         return ESP_FAIL;
    //     }
    //     const char *label = static_cast<const char *>(priv_data);
    //     if (label) {
    //         cluster_t *bdbi = cluster::get(ep, chip::app::Clusters::BridgedDeviceBasicInformation::Id);
    //         if (bdbi) {
    //             bridged_device_basic_information::attribute::create_node_label(
    //                 bdbi, (char *)label, strlen(label));
    //         }
    //     }
    //     return ESP_OK;
    // }

    ESP_LOGE(TAG, "Unsupported device type: 0x%08" PRIx32, device_type_id);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t ModbusManager::register_device(const device_config_t &config)
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();

    // --- Solar Power endpoint ---
    esp_matter_bridge::device_t *solar_dev = nullptr;
    if (config.matter_endpoint_id != MATTER_ENDPOINT_ID_INVALID) {
        solar_dev = esp_matter_bridge::resume_device(m_node, config.matter_endpoint_id,
                                                     const_cast<device_config_t *>(&config));
        if (!solar_dev) {
            ESP_LOGW(TAG, "[%s] resume solar ep=%u failed, recreating",
                     config.id, config.matter_endpoint_id);
        }
    }
    if (!solar_dev) {
        solar_dev = esp_matter_bridge::create_device(m_node, 
                                                     m_aggregator_endpoint_id,
                                                     ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID,
                                                     const_cast<device_config_t *>(&config));
        if (!solar_dev) {
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
            ESP_LOGE(TAG, "[%s] create solar device failed", config.id);
            return ESP_FAIL;
        }
        devices_store_set_endpoint_id(config.id, endpoint::get_id(solar_dev->endpoint));
    }

    endpoint::enable(solar_dev->endpoint);

    // --- PV String 1 endpoint ---
    // char pv1_label[DEVICE_NAME_LEN + 8];
    // snprintf(pv1_label, sizeof(pv1_label), "%s PV1", config.name);

    // esp_matter_bridge::device_t *pv1_dev = nullptr;
    // if (config.pv1_endpoint_id != MATTER_ENDPOINT_ID_INVALID) {
    //     pv1_dev = esp_matter_bridge::resume_device(m_node, config.pv1_endpoint_id, pv1_label);
    //     if (!pv1_dev) {
    //         ESP_LOGW(TAG, "[%s] resume PV1 ep=%u failed, recreating",
    //                  config.id, config.pv1_endpoint_id);
    //     }
    // }
    // if (!pv1_dev) {
    //     pv1_dev = esp_matter_bridge::create_device(m_node, m_aggregator_endpoint_id,
    //                                                ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID,
    //                                                pv1_label);
    //     if (!pv1_dev) {
    //         chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    //         ESP_LOGE(TAG, "[%s] create PV1 device failed", config.id);
    //         return ESP_FAIL;
    //     }
    // }
    // endpoint::enable(pv1_dev->endpoint);

    // // --- PV String 2 endpoint ---
    // char pv2_label[DEVICE_NAME_LEN + 8];
    // snprintf(pv2_label, sizeof(pv2_label), "%s PV2", config.name);

    // esp_matter_bridge::device_t *pv2_dev = nullptr;
    // if (config.pv2_endpoint_id != MATTER_ENDPOINT_ID_INVALID) {
    //     pv2_dev = esp_matter_bridge::resume_device(m_node, config.pv2_endpoint_id, pv2_label);
    //     if (!pv2_dev) {
    //         ESP_LOGW(TAG, "[%s] resume PV2 ep=%u failed, recreating",
    //                  config.id, config.pv2_endpoint_id);
    //     }
    // }
    // if (!pv2_dev) {
    //     pv2_dev = esp_matter_bridge::create_device(m_node, m_aggregator_endpoint_id,
    //                                                ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID,
    //                                                pv2_label);
    //     if (!pv2_dev) {
    //         chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    //         ESP_LOGE(TAG, "[%s] create PV2 device failed", config.id);
    //         return ESP_FAIL;
    //     }
    // }
    // endpoint::enable(pv2_dev->endpoint);

    // Persist PV endpoint IDs if newly created
    // uint16_t pv1_ep = endpoint::get_id(pv1_dev->endpoint);
    // uint16_t pv2_ep = endpoint::get_id(pv2_dev->endpoint);
    // if (config.pv1_endpoint_id == MATTER_ENDPOINT_ID_INVALID ||
    //     config.pv2_endpoint_id == MATTER_ENDPOINT_ID_INVALID) {
    //     devices_store_set_pv_endpoint_ids(config.id, pv1_ep, pv2_ep);
    // }

    //devices_store_set_pv_endpoint_ids(config.id);

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    auto *modbus = new ModbusDevice(config);
    auto *solar  = new SolarPowerDevice(solar_dev, config);
    // auto *pv1    = new ElectricalSensorDevice(pv1_dev, pv1_label);
    // auto *pv2    = new ElectricalSensorDevice(pv2_dev, pv2_label);
    auto *arg    = new ReadingsArg{solar};

    // modbus->set_readings_callback(readings_update_cb, arg);
    m_devices.push_back({modbus, solar, solar_dev, arg});

    // ESP_LOGI(TAG, "Registered '%s' (%s:%u) -> solar ep=%u pv1 ep=%u pv2 ep=%u",
    //          config.name, config.host, config.port,
    //          solar->endpoint_id(), pv1_ep, pv2_ep);
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
    //delete it->pv1;
    //delete it->pv2;
    delete it->readings_arg;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_matter_bridge::remove_device(it->bridge_dev);
    //esp_matter_bridge::remove_device(it->pv1_bridge_dev);
    //esp_matter_bridge::remove_device(it->pv2_bridge_dev);
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
        //delete pair.pv1;
        //delete pair.pv2;
        delete pair.readings_arg;
        esp_matter_bridge::remove_device(pair.bridge_dev);
        //esp_matter_bridge::remove_device(pair.pv1_bridge_dev);
        //esp_matter_bridge::remove_device(pair.pv2_bridge_dev);
    }
    esp_matter_bridge::factory_reset();
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
    //out.pv1_voltage_valid = it->pv1->get_voltage_mv(out.pv1_voltage_mv);
    //out.pv1_current_valid = it->pv1->get_active_current_ma(out.pv1_current_ma);
    //out.pv2_voltage_valid = it->pv2->get_voltage_mv(out.pv2_voltage_mv);
    //out.pv2_current_valid = it->pv2->get_active_current_ma(out.pv2_current_ma);
    return true;
}
