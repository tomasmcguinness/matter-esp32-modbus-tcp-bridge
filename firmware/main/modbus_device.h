#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "devices_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum class ModbusConnectionState {
    Disconnected,
    Connected,
    Failed,
};

class ModbusDevice {
public:
    explicit ModbusDevice(const device_config_t &config);
    ~ModbusDevice();

    const char           *id()    const { return m_config.id; }
    const char           *name()  const { return m_config.name; }
    ModbusConnectionState state() const { return m_state; }

    void      start_polling();
    esp_err_t read_input_registers(uint16_t reg_addr, uint16_t count, uint16_t *out);
    esp_err_t read_holding_registers(uint16_t reg_addr, uint16_t count, uint16_t *out);

    typedef void (*voltage_cb_t)(uint16_t raw_value, void *arg);
    void set_voltage_callback(voltage_cb_t cb, void *arg);

private:
    device_config_t       m_config;
    int                   m_sock;
    uint16_t              m_transaction_id;
    ModbusConnectionState m_state;
    TaskHandle_t          m_poll_task;
    voltage_cb_t          m_voltage_cb;
    void                 *m_voltage_cb_arg;

    esp_err_t ensure_connected();
    esp_err_t send_request(uint8_t func_code, uint16_t reg_addr, uint16_t count, uint16_t *out);
    void      close_socket();

    static void poll_task(void *arg);
};
