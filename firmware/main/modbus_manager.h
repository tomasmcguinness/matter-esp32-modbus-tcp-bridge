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

    void on_device_added(const device_config_t &config, ModbusDevice::readings_cb_t cb = nullptr);
    void on_device_removed(const char *id);
    void clear();

    ModbusDevice *find(const char *id) const;

    // ---- Ad-hoc (transient) access for the Add Device wizard ----
    // These open a fresh short-lived connection and do NOT create a persisted
    // device or a polling task. Used by POST /api/modbus/test-connection and
    // POST /api/modbus/read.

    struct ReadReq  { uint16_t address; bool input; };
    struct ReadResp { uint16_t address; bool input; bool ok; uint16_t value; };

    // Verify the device is reachable by reading a single register. ESP_OK on success.
    esp_err_t test_connection(const char *host, uint16_t port, uint8_t unit_id);

    // Read each requested register over one transient connection, appending a
    // ReadResp (with an `ok` flag) per request. Returns ESP_OK if the connection
    // could be established (at least one read succeeded), ESP_FAIL otherwise.
    esp_err_t read_registers(const char *host, uint16_t port, uint8_t unit_id,
                             const std::vector<ReadReq> &reqs,
                             std::vector<ReadResp> &out);

private:
    ModbusManager() = default;

    void create_device(const device_config_t &config, const std::vector<RegisterSpec> &regs,
                       ModbusDevice::readings_cb_t cb);

    std::vector<ModbusDevice *> m_devices;
};
