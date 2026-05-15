#pragma once

#include <vector>
#include "esp_err.h"
#include "devices_store.h"
#include "modbus_device.h"

class ModbusManager {
public:
    static ModbusManager &instance();

    void init();
    void start_polling();
    void pause_all();
    void resume_all();

    void on_device_added(const device_config_t &config, const std::vector<RegisterSpec> &regs);
    void on_device_removed(const char *id);
    void clear();

    ModbusDevice *find(const char *id) const;

private:
    ModbusManager() = default;

    void create_device(const device_config_t &config, const std::vector<RegisterSpec> &regs);

    std::vector<ModbusDevice *> m_devices;
};
