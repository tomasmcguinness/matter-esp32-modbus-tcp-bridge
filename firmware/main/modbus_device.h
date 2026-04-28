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

    struct Readings {
        uint16_t voltage_raw;  // 0.1 V units
        int16_t  current_raw;  // 0.1 A units, signed
        int16_t  power_raw;    // W units, signed
    };
    typedef void (*readings_cb_t)(const Readings &readings, void *arg);
    void set_readings_callback(readings_cb_t cb, void *arg);

private:
    device_config_t       m_config;
    int                   m_sock;
    uint16_t              m_transaction_id;
    ModbusConnectionState m_state;
    TaskHandle_t          m_poll_task;
    readings_cb_t         m_readings_cb;
    void                 *m_readings_cb_arg;

    esp_err_t ensure_connected();
    esp_err_t send_request(uint8_t func_code, uint16_t reg_addr, uint16_t count, uint16_t *out);
    void      close_socket();

    static void poll_task(void *arg);
};
