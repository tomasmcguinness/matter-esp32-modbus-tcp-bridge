#include "modbus_device.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "esp_log.h"

static const char *TAG = "modbus_device";

// Modbus spec: max 125 registers per read request
#define MODBUS_MAX_REGISTERS 125

#define POLL_INTERVAL_MS  5000
// Temporary: read Grid Voltage only. Full register mapping comes with TASK-7.
#define REG_GRID_VOLTAGE  0x0000

ModbusDevice::ModbusDevice(const device_config_t &config)
    : m_config(config), m_sock(-1), m_transaction_id(0),
      m_state(ModbusConnectionState::Disconnected), m_poll_task(nullptr),
      m_voltage_cb(nullptr), m_voltage_cb_arg(nullptr)
{
}

void ModbusDevice::set_voltage_callback(voltage_cb_t cb, void *arg)
{
    m_voltage_cb     = cb;
    m_voltage_cb_arg = arg;
}

ModbusDevice::~ModbusDevice()
{
    if (m_poll_task) {
        vTaskDelete(m_poll_task);
        m_poll_task = nullptr;
    }
    close_socket();
}

void ModbusDevice::start_polling()
{
    xTaskCreate(poll_task, "mb_poll", 4096, this, 5, &m_poll_task);
}

void ModbusDevice::poll_task(void *arg)
{
    ModbusDevice *self = static_cast<ModbusDevice *>(arg);
    while (true) {
        uint16_t value = 0;
        if (self->read_input_registers(REG_GRID_VOLTAGE, 1, &value) == ESP_OK) {
            ESP_LOGI(TAG, "[%s] Grid Voltage: %u (raw)", self->m_config.id, value);
            if (self->m_voltage_cb) {
                self->m_voltage_cb(value, self->m_voltage_cb_arg);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void ModbusDevice::close_socket()
{
    if (m_sock >= 0) {
        close(m_sock);
        m_sock = -1;
    }
    m_state = ModbusConnectionState::Disconnected;
}

esp_err_t ModbusDevice::ensure_connected()
{
    if (m_sock >= 0) return ESP_OK;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", m_config.port);

    struct addrinfo hints = {};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (getaddrinfo(m_config.host, port_str, &hints, &res) != 0 || res == nullptr) {
        ESP_LOGE(TAG, "[%s] DNS lookup failed for %s", m_config.id, m_config.host);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "[%s] socket() failed: %d", m_config.id, errno);
        freeaddrinfo(res);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "[%s] connect(%s:%u) failed: %d", m_config.id, m_config.host, m_config.port, errno);
        freeaddrinfo(res);
        close(sock);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    freeaddrinfo(res);
    m_sock  = sock;
    m_state = ModbusConnectionState::Connected;
    ESP_LOGI(TAG, "[%s] connected to %s:%u", m_config.id, m_config.host, m_config.port);
    return ESP_OK;
}

esp_err_t ModbusDevice::send_request(uint8_t func_code, uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    if (count == 0 || count > MODBUS_MAX_REGISTERS) return ESP_ERR_INVALID_ARG;
    if (ensure_connected() != ESP_OK) return ESP_FAIL;

    uint16_t tid = ++m_transaction_id;

    // MBAP header (6 bytes) + Unit ID + FC + Start addr (2) + Count (2)
    uint8_t req[12];
    req[0]  = tid >> 8;
    req[1]  = tid & 0xFF;
    req[2]  = 0;                 // Protocol ID high
    req[3]  = 0;                 // Protocol ID low
    req[4]  = 0;                 // Length high
    req[5]  = 6;                 // Length low: unit_id(1) + FC(1) + addr(2) + count(2)
    req[6]  = m_config.unit_id;
    req[7]  = func_code;
    req[8]  = reg_addr >> 8;
    req[9]  = reg_addr & 0xFF;
    req[10] = count >> 8;
    req[11] = count & 0xFF;

    int sent = 0;
    while (sent < (int)sizeof(req)) {
        int n = send(m_sock, req + sent, sizeof(req) - sent, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "[%s] send failed: %d", m_config.id, errno);
            close_socket();
            return ESP_FAIL;
        }
        sent += n;
    }

    // Read the fixed 9-byte response prefix: 6 MBAP + unit_id + FC + byte_count/exception_code
    uint8_t header[9];
    int received = 0;
    while (received < (int)sizeof(header)) {
        int n = recv(m_sock, header + received, sizeof(header) - received, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "[%s] recv header failed: %d", m_config.id, errno);
            close_socket();
            return ESP_FAIL;
        }
        received += n;
    }

    if (header[7] & 0x80) {
        ESP_LOGE(TAG, "[%s] Modbus exception: FC=0x%02x code=%u", m_config.id, header[7], header[8]);
        return ESP_FAIL;
    }

    // header[8] is the byte count; read that many data bytes
    int data_len = header[8];
    uint8_t data[MODBUS_MAX_REGISTERS * 2];
    received = 0;
    while (received < data_len) {
        int n = recv(m_sock, data + received, data_len - received, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "[%s] recv data failed: %d", m_config.id, errno);
            close_socket();
            return ESP_FAIL;
        }
        received += n;
    }

    for (int i = 0; i < (int)count; i++) {
        out[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
    }

    return ESP_OK;
}

esp_err_t ModbusDevice::read_input_registers(uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    return send_request(0x04, reg_addr, count, out);
}

esp_err_t ModbusDevice::read_holding_registers(uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    return send_request(0x03, reg_addr, count, out);
}
