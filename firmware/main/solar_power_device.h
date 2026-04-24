#pragma once

#include <stdint.h>
#include "esp_matter.h"
#include "devices_store.h"

#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>

class SolarPowerDevice : public chip::app::Clusters::ElectricalPowerMeasurement::Delegate {
public:
    SolarPowerDevice(esp_matter::node_t *node,
                     esp_matter::endpoint_t *aggregator,
                     const device_config_t &config);
    ~SolarPowerDevice();

    void set_voltage(uint16_t raw_value);

    uint16_t endpoint_id() const { return m_endpoint_id; }

    // Delegate overrides
    chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum GetPowerMode() override {
        return chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum::kAc;
    }
    uint8_t GetNumberOfMeasurementTypes() override { return 2; }

    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type &accuracy) override;
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementRangeStruct::Type &) override {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::HarmonicMeasurementStruct::Type &) override {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::HarmonicMeasurementStruct::Type &) override {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    chip::app::DataModel::Nullable<int64_t> GetVoltage() override { return m_voltage; }
    chip::app::DataModel::Nullable<int64_t> GetActiveCurrent() override { return m_active_current; }
    chip::app::DataModel::Nullable<int64_t> GetReactiveCurrent() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetApparentCurrent() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetActivePower() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetReactivePower() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetApparentPower() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetRMSVoltage() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetRMSCurrent() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetRMSPower() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetFrequency() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetPowerFactor() override { return chip::app::DataModel::NullNullable; }
    chip::app::DataModel::Nullable<int64_t> GetNeutralCurrent() override { return chip::app::DataModel::NullNullable; }

private:
    uint16_t m_endpoint_id = 0;
    chip::app::DataModel::Nullable<int64_t> m_voltage;
    chip::app::DataModel::Nullable<int64_t> m_active_current;
    chip::app::Clusters::ElectricalPowerMeasurement::Instance *m_epm_instance = nullptr;
};
