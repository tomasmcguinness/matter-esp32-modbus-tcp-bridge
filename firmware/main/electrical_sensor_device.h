#pragma once

#include <stdint.h>
#include "esp_matter.h"
#include <esp_matter_bridge.h>
#include "devices_store.h"
#include "matter_device.h"

#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>

class ElectricalSensorDevice : public IMatterDevice, public chip::app::Clusters::ElectricalPowerMeasurement::Delegate {
public:
    ElectricalSensorDevice(esp_matter_bridge::device_t *dev, const device_config_t &config);
    ~ElectricalSensorDevice();

    void set_voltage(uint16_t raw_value);        // 0.1 V units → mV
    void set_active_current(int16_t raw_value);  // 0.1 A units → mA
    void set_active_power(int16_t raw_value);    // 0.1 W units → mW

    void set_raw(uint32_t cluster_id, uint32_t attribute_id, uint16_t raw_value) override;
    bool get_raw(uint32_t cluster_id, uint32_t attribute_id, int64_t &out) const override;

    uint16_t endpoint_id() const override { return m_endpoint_id; }

    bool get_voltage_mv(int64_t &out) const {
        if (m_voltage.IsNull()) return false;
        out = m_voltage.Value();
        return true;
    }
    bool get_active_current_ma(int64_t &out) const {
        if (m_active_current.IsNull()) return false;
        out = m_active_current.Value();
        return true;
    }
    bool get_active_power_mw(int64_t &out) const {
        if (m_active_power.IsNull()) return false;
        out = m_active_power.Value();
        return true;
    }

    chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum GetPowerMode() override {
        return chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum::kDc;
    }
    uint8_t GetNumberOfMeasurementTypes() override { return 3; }

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
    chip::app::DataModel::Nullable<int64_t> GetActivePower() override { return m_active_power; }
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
    chip::app::DataModel::Nullable<int64_t> m_active_power;
    chip::app::Clusters::ElectricalPowerMeasurement::Instance *m_epm_instance = nullptr;
};
