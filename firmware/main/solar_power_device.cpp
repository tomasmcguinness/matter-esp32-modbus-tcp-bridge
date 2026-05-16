#include "solar_power_device.h"

#include <string.h>

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_data_model.h"
#include <esp_matter_bridge.h>

#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>

static const char *TAG = "solar_power_device";

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;
using namespace chip::app::Clusters::ElectricalPowerMeasurement::Attributes;

static const Structs::MeasurementAccuracyRangeStruct::Type kVoltageRanges[] = {{
    .rangeMin = 0,
    .rangeMax = 300'000,
    .percentMax = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyRangeStruct::Type kCurrentRanges[] = {{
    .rangeMin = -100'000,
    .rangeMax = 100'000,
    .percentMax = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyRangeStruct::Type kPowerRanges[] = {{
    .rangeMin = -30'000'000,
    .rangeMax = 30'000'000,
    .percentMax = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyStruct::Type kAccuracies[] = {
    {
        .measurementType = MeasurementTypeEnum::kVoltage,
        .measured = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 300'000,
        .accuracyRanges = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kVoltageRanges),
    },
    {
        .measurementType = MeasurementTypeEnum::kActiveCurrent,
        .measured = true,
        .minMeasuredValue = -100'000,
        .maxMeasuredValue = 100'000,
        .accuracyRanges = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kCurrentRanges),
    },
    {
        .measurementType = MeasurementTypeEnum::kActivePower,
        .measured = true,
        .minMeasuredValue = -30'000'000,
        .maxMeasuredValue = 30'000'000,
        .accuracyRanges = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kPowerRanges),
    },
};

CHIP_ERROR SolarPowerDevice::GetAccuracyByIndex(uint8_t index, Structs::MeasurementAccuracyStruct::Type &accuracy)
{
    if (index >= MATTER_ARRAY_SIZE(kAccuracies))
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    accuracy = kAccuracies[index];
    return CHIP_NO_ERROR;
}

SolarPowerDevice::SolarPowerDevice(esp_matter_bridge::device_t *dev, const device_config_t &config)
    : m_endpoint_id(0)
{
    if (!dev || !dev->endpoint)
    {
        ESP_LOGE(TAG, "[%s] Invalid bridge device", config.id);
        return;
    }

    uint16_t ep_id = endpoint::get_id(dev->endpoint);

    chip::BitMask<OptionalAttributes> optional_attrs;
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeVoltage);
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeActiveCurrent);

    m_epm_instance = new Instance(
        ep_id, *this,
        chip::BitMask<Feature>(Feature::kAlternatingCurrent),
        optional_attrs);
    m_epm_instance->Init();

    m_endpoint_id = ep_id;
    ESP_LOGI(TAG, "[%s] Solar Power Device instance attached: ep=%u", config.id, m_endpoint_id);
}

SolarPowerDevice::~SolarPowerDevice()
{
    if (m_epm_instance)
    {
        m_epm_instance->Shutdown();
        delete m_epm_instance;
    }
}

void SolarPowerDevice::set_voltage(uint16_t raw_value)
{
        // Solax reports voltage in 0.1V units; Matter expects millivolts
    auto mv = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 100);
    if (m_voltage == mv)
        return;
    m_voltage = mv;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg)
        {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::Voltage::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}

void SolarPowerDevice::set_active_current(int16_t raw_value)
{
    // Solax reports current in 0.1A units (signed); Matter expects milliamps
    auto ma = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 100);
    if (m_active_current == ma)
        return;
    m_active_current = ma;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg)
        {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::ActiveCurrent::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}

void SolarPowerDevice::set_active_power(int16_t raw_value)
{
    // Solax reports power in W (signed); Matter expects milliwatts
    auto mw = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 1000);
    if (m_active_power == mw)
        return;
    m_active_power = mw;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg)
        {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::ActivePower::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}

void SolarPowerDevice::set_raw(uint32_t cluster_id, uint32_t attribute_id, uint16_t raw_value)
{
    ESP_LOGI(TAG, "Received raw value update: cluster=0x%08" PRIx32 ", attribute=0x%08" PRIx32 ", value=%u", cluster_id, attribute_id, raw_value);

    if (cluster_id == ElectricalPowerMeasurement::Id)
    {
        if (attribute_id == Attributes::Voltage::Id)
            set_voltage(raw_value);
        else if (attribute_id == Attributes::ActiveCurrent::Id)
            set_active_current((int16_t)raw_value);
        else if (attribute_id == Attributes::ActivePower::Id)
            set_active_power((int16_t)raw_value);
    }
}

bool SolarPowerDevice::get_raw(uint32_t cluster_id, uint32_t attribute_id, int64_t &out) const
{
    ESP_LOGI(TAG, "Getting raw value for cluster cluster=0x%08" PRIx32 ", attribute=0x%08" PRIx32, cluster_id, attribute_id);

    if (cluster_id == ElectricalPowerMeasurement::Id)
    {
        if (attribute_id == Attributes::Voltage::Id)
            return get_voltage_mv(out);
        if (attribute_id == Attributes::ActiveCurrent::Id)
            return get_active_current_ma(out);
        if (attribute_id == Attributes::ActivePower::Id)
            return get_active_power_mw(out);
    }

    return false;
}


