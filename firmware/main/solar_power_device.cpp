#include "solar_power_device.h"

#include <string.h>

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_data_model.h"

#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>
#include <clusters/BridgedDeviceBasicInformation/ClusterId.h>

static const char *TAG = "solar_power_device";

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;

static const Structs::MeasurementAccuracyRangeStruct::Type kVoltageRanges[] = {{
    .rangeMin      = 0,
    .rangeMax      = 300'000,
    .percentMax    = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin    = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyRangeStruct::Type kCurrentRanges[] = {{
    .rangeMin      = 0,
    .rangeMax      = 100'000,
    .percentMax    = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin    = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyStruct::Type kAccuracies[] = {
    {
        .measurementType   = MeasurementTypeEnum::kVoltage,
        .measured          = true,
        .minMeasuredValue  = 0,
        .maxMeasuredValue  = 300'000,
        .accuracyRanges    = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kVoltageRanges),
    },
    {
        .measurementType   = MeasurementTypeEnum::kActiveCurrent,
        .measured          = true,
        .minMeasuredValue  = 0,
        .maxMeasuredValue  = 100'000,
        .accuracyRanges    = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kCurrentRanges),
    },
};

CHIP_ERROR SolarPowerDevice::GetAccuracyByIndex(uint8_t index, Structs::MeasurementAccuracyStruct::Type &accuracy)
{
    if (index >= MATTER_ARRAY_SIZE(kAccuracies))
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    accuracy = kAccuracies[index];
    return CHIP_NO_ERROR;
}

SolarPowerDevice::SolarPowerDevice(node_t *node,
                                   endpoint_t *aggregator,
                                   const device_config_t &config)
    : m_endpoint_id(0)
{
    bridged_node::config_t node_cfg;
    node_cfg.bridged_device_basic_information.reachable = true;
    endpoint_t *ep = bridged_node::create(node, &node_cfg,
                                          ENDPOINT_FLAG_DESTROYABLE | ENDPOINT_FLAG_BRIDGE,
                                          nullptr);
    if (!ep) {
        ESP_LOGE(TAG, "[%s] Failed to create bridged_node endpoint", config.id);
        return;
    }

    // solar_power::add() sets AC feature flag and creates voltage + active_current attributes.
    // Leave config.delegate nullptr — we create the Instance ourselves below.
    solar_power::config_t sp_cfg;
    if (solar_power::add(ep, &sp_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "[%s] Failed to add solar_power clusters", config.id);
        return;
    }

    cluster_t *bdbi = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    if (bdbi) {
        char name[DEVICE_NAME_LEN];
        strncpy(name, config.name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        bridged_device_basic_information::attribute::create_node_label(bdbi, name, strlen(name));
    }

    if (set_parent_endpoint(ep, aggregator) != ESP_OK) {
        ESP_LOGE(TAG, "[%s] Failed to set parent endpoint", config.id);
    }

    endpoint::enable(ep);

    // Run plugin server init for every cluster on this endpoint (EPM has none, but others may).
    cluster_t *cl = cluster::get_first(ep);
    while (cl) {
        cluster::plugin_server_init_callback_t plugin_cb = cluster::get_plugin_server_init_callback(cl);
        if (plugin_cb) plugin_cb();
        cl = cluster::get_next(cl);
    }

    uint16_t ep_id = endpoint::get_id(ep);

    // Build optional-attrs mask to match the attributes solar_power::add() created.
    chip::BitMask<OptionalAttributes> optional_attrs;
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeVoltage);
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeActiveCurrent);

    m_epm_instance = new Instance(
        ep_id, *this,
        chip::BitMask<Feature>(Feature::kAlternatingCurrent),
        optional_attrs);
    m_epm_instance->Init();

    m_endpoint_id = ep_id;
    ESP_LOGI(TAG, "[%s] Solar Power bridged endpoint created: ep=%u", config.id, m_endpoint_id);
}

SolarPowerDevice::~SolarPowerDevice()
{
    if (m_epm_instance) {
        m_epm_instance->Shutdown();
        delete m_epm_instance;
    }
}

void SolarPowerDevice::set_voltage(uint16_t raw_value)
{
    // Solax reports voltage in 0.1V units; Matter expects millivolts
    auto mv = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 100);
    if (m_voltage == mv) return;
    m_voltage = mv;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::Voltage::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}
