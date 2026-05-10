#include "electrical_sensor_device.h"

#include "esp_log.h"
#include "esp_matter.h"
#include "esp_matter_data_model.h"
#include <esp_matter_bridge.h>

#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>

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
};

CHIP_ERROR ElectricalSensorDevice::GetAccuracyByIndex(uint8_t index,
    Structs::MeasurementAccuracyStruct::Type &accuracy)
{
    if (index >= MATTER_ARRAY_SIZE(kAccuracies))
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    accuracy = kAccuracies[index];
    return CHIP_NO_ERROR;
}

ElectricalSensorDevice::ElectricalSensorDevice(esp_matter_bridge::device_t *dev, const char *label)
    : m_endpoint_id(0)
{
    if (!dev || !dev->endpoint) {
        ESP_LOGE(TAG, "[%s] Invalid bridge device", label);
        return;
    }

    uint16_t ep_id = endpoint::get_id(dev->endpoint);

    chip::BitMask<OptionalAttributes> optional_attrs;
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeVoltage);
    optional_attrs.Set(OptionalAttributes::kOptionalAttributeActiveCurrent);

    m_epm_instance = new Instance(
        ep_id, *this,
        chip::BitMask<Feature>(Feature::kDirectCurrent),
        optional_attrs);
    m_epm_instance->Init();

    m_endpoint_id = ep_id;
    ESP_LOGI(TAG, "[%s] EPM instance attached: ep=%u", label, m_endpoint_id);
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

void ElectricalSensorDevice::set_active_current(uint16_t raw_value)
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
