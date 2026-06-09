#include "electrical_sensor_device.h"

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_data_model.h"
#include <esp_matter_bridge.h>

#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>

#include <clusters/PowerSource/ClusterId.h>
#include <clusters/PowerSource/AttributeIds.h>

static const char *TAG = "electrical_sensor_device";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;
using namespace chip::app::Clusters::ElectricalPowerMeasurement::Attributes;

static const Structs::MeasurementAccuracyRangeStruct::Type kVoltageRanges[] = {{
    .rangeMin       = 0,
    .rangeMax       = 1'000'000,
    .percentMax     = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin     = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyRangeStruct::Type kCurrentRanges[] = {{
    .rangeMin       = 0,
    .rangeMax       = 20'000,
    .percentMax     = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin     = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyRangeStruct::Type kPowerRanges[] = {{
    .rangeMin       = -30'000'000,
    .rangeMax       = 30'000'000,
    .percentMax     = chip::MakeOptional(static_cast<chip::Percent100ths>(1000)),
    .percentMin     = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    .percentTypical = chip::MakeOptional(static_cast<chip::Percent100ths>(500)),
}};

static const Structs::MeasurementAccuracyStruct::Type kAccuracies[] = {
    {
        .measurementType  = MeasurementTypeEnum::kVoltage,
        .measured         = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 1'000'000,
        .accuracyRanges   = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kVoltageRanges),
    },
    {
        .measurementType  = MeasurementTypeEnum::kActiveCurrent,
        .measured         = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 20'000,
        .accuracyRanges   = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kCurrentRanges),
    },
    {
        .measurementType  = MeasurementTypeEnum::kActivePower,
        .measured         = true,
        .minMeasuredValue = -30'000'000,
        .maxMeasuredValue = 30'000'000,
        .accuracyRanges   = chip::app::DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(kPowerRanges),
    },
};

CHIP_ERROR ElectricalSensorDevice::GetAccuracyByIndex(uint8_t index,
    Structs::MeasurementAccuracyStruct::Type &accuracy)
{
    if (index >= MATTER_ARRAY_SIZE(kAccuracies))
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    accuracy = kAccuracies[index];
    return CHIP_NO_ERROR;
}

ElectricalSensorDevice::ElectricalSensorDevice(esp_matter::endpoint_t *ep, const device_config_t &config)
    : m_endpoint_id(0)
{
    if (!ep)
    {
        ESP_LOGE(TAG, "[%s] Invalid endpoint", config.id);
        return;
    }

    uint16_t ep_id = endpoint::get_id(ep);

    chip::BitMask<OptionalAttributes> optional_attrs;
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeVoltage);
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeActiveCurrent);

    m_epm_instance = new Instance(
        ep_id, *this,
        chip::BitMask<Feature>(Feature::kDirectCurrent),
        optional_attrs);
    m_epm_instance->Init();

    m_endpoint_id = ep_id;
    ESP_LOGI(TAG, "[%s] ElectricalSensor instance attached: ep=%u", config.id, m_endpoint_id);
}

ElectricalSensorDevice::~ElectricalSensorDevice()
{
    if (m_epm_instance) {
        m_epm_instance->Shutdown();
        delete m_epm_instance;
    }
}

void ElectricalSensorDevice::set_voltage(uint16_t raw_value)
{
    // 0.1 V units → millivolts
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

void ElectricalSensorDevice::set_active_current(int16_t raw_value)
{
    // 0.1 A units → milliamps
    auto ma = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 100);
    if (m_active_current == ma) return;
    m_active_current = ma;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::ActiveCurrent::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}

void ElectricalSensorDevice::set_active_power(int16_t raw_value)
{
    // W units → milliwatts
    auto mw = chip::app::DataModel::MakeNullable(static_cast<int64_t>(raw_value) * 1000);
    if (m_active_power == mw) return;
    m_active_power = mw;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            MatterReportingAttributeChangeCallback(
                static_cast<chip::EndpointId>(arg),
                ElectricalPowerMeasurement::Id,
                Attributes::ActivePower::Id);
        },
        static_cast<intptr_t>(m_endpoint_id));
}

void ElectricalSensorDevice::set_bat_percent_remaining(uint16_t raw_value)
{
    // Solax reports SoC as plain percent (0-100); Matter BatPercentRemaining is 0-200 half-percent.
    uint16_t scaled = raw_value * 2;
    if (scaled > 200)
        scaled = 200;
    uint8_t scaled8 = static_cast<uint8_t>(scaled);
    auto pct = chip::app::DataModel::MakeNullable(scaled8);
    if (m_bat_percent == pct)
        return;
    m_bat_percent = pct;

    // The PowerSource cluster has no Delegate/Instance here; write straight to the attribute store.
    // esp_matter::attribute::update takes the chip stack lock internally (safe from this task).
    esp_matter_attr_val_t val = esp_matter_nullable_uint8(scaled8);
    esp_matter::attribute::update(m_endpoint_id, PowerSource::Id,
                                  PowerSource::Attributes::BatPercentRemaining::Id, &val);
}

bool ElectricalSensorDevice::get_raw(uint32_t cluster_id, uint32_t attribute_id, int64_t &out) const
{
    ESP_LOGI(TAG, "Getting raw value for cluster=0x%08" PRIx32 ", attribute=0x%08" PRIx32, cluster_id, attribute_id);

    if (cluster_id == ElectricalPowerMeasurement::Id)
    {
        if (attribute_id == Attributes::Voltage::Id)
            return get_voltage_mv(out);
        if (attribute_id == Attributes::ActiveCurrent::Id)
            return get_active_current_ma(out);
        if (attribute_id == Attributes::ActivePower::Id)
            return get_active_power_mw(out);
    }
    else if (cluster_id == PowerSource::Id)
    {
        if (attribute_id == PowerSource::Attributes::BatPercentRemaining::Id)
            return get_bat_percent_remaining(out);
    }

    return false;
}

void ElectricalSensorDevice::set_raw(uint32_t cluster_id, uint32_t attribute_id, uint16_t raw_value)
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
    else if (cluster_id == PowerSource::Id)
    {
        if (attribute_id == PowerSource::Attributes::BatPercentRemaining::Id)
            set_bat_percent_remaining(raw_value);
    }
}
