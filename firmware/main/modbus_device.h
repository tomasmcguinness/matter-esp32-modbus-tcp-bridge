#pragma once

#include <stdint.h>
#include <vector>
#include "esp_err.h"
#include "devices_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum class ModbusConnectionState {
    Disconnected,
    Connected,
    Failed,
};

struct RegisterSpec {
    uint16_t address;
    bool     input; // true = input register (FC4), false = holding (FC3)
};

struct RegisterReading {
    uint16_t address;
    uint16_t value;
};

class ModbusDevice {
public:
    explicit ModbusDevice(const device_config_t &config, std::vector<RegisterSpec> regs);
    ~ModbusDevice();

    const char           *id()    const { return m_config.id; }
    const char           *name()  const { return m_config.name; }
    ModbusConnectionState state() const { return m_state; }

    void      start_polling();
    void      pause();
    void      resume();

    esp_err_t read_input_registers(uint16_t reg_addr, uint16_t count, uint16_t *out);
    esp_err_t read_holding_registers(uint16_t reg_addr, uint16_t count, uint16_t *out);

    typedef void (*readings_cb_t)(const std::vector<RegisterReading> &readings, void *arg);
    void set_readings_callback(readings_cb_t cb, void *arg);

private:
    device_config_t            m_config;
    std::vector<RegisterSpec>  m_regs;
    int                        m_sock;
    uint16_t                   m_transaction_id;
    ModbusConnectionState      m_state;
    TaskHandle_t               m_poll_task;
    readings_cb_t              m_readings_cb;
    void                      *m_readings_cb_arg;
    volatile bool              m_paused;

    esp_err_t ensure_connected();
    esp_err_t send_request(uint8_t func_code, uint16_t reg_addr, uint16_t count, uint16_t *out);
    void      close_socket();

    static void poll_task(void *arg);
};
