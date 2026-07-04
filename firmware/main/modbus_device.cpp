#include "modbus_device.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include "esp_log.h"

static const char *TAG = "modbus_device";

// Modbus spec: max 125 registers per read request
#define MODBUS_MAX_REGISTERS 125

#define POLL_INTERVAL_MS 5000

// Bound socket operations so an unreachable host can't block the caller
// indefinitely (matters for the ad-hoc test-connection / read endpoints, which
// run on the HTTP server task).
#define MODBUS_CONNECT_TIMEOUT_S 3
#define MODBUS_IO_TIMEOUT_S      3

ModbusDevice::ModbusDevice(const device_config_t &config, std::vector<RegisterSpec> regs)
    : m_config(config), m_regs(std::move(regs)), m_sock(-1), m_transaction_id(0),
      m_state(ModbusConnectionState::Disconnected), m_poll_task(nullptr),
      m_readings_cb(nullptr), m_readings_cb_arg(nullptr), m_paused(false)
{
}

void ModbusDevice::set_readings_callback(readings_cb_t cb, void *arg)
{
    m_readings_cb = cb;
    m_readings_cb_arg = arg;
}

ModbusDevice::~ModbusDevice()
{
    if (m_poll_task)
    {
        vTaskDelete(m_poll_task);
        m_poll_task = nullptr;
    }
    close_socket();
}

void ModbusDevice::start_polling()
{
    xTaskCreate(poll_task, "mb_poll", 4096, this, 5, &m_poll_task);
}

void ModbusDevice::pause()
{
    m_paused = true;
    close_socket();
    ESP_LOGI(TAG, "[%s] polling paused (link down)", m_config.id);
}

void ModbusDevice::resume()
{
    m_paused = false;
    ESP_LOGI(TAG, "[%s] polling resumed (link up)", m_config.id);
}

// Group consecutive same-func-code specs into one bulk read, then extract the
// requested addresses from the result buffer.
static void read_group(ModbusDevice *self, const std::vector<RegisterSpec> &specs,
                       bool input, std::vector<RegisterReading> &out)
{
    uint16_t min_addr = UINT16_MAX, max_addr = 0;
    for (const auto &s : specs) {
        if (s.input != input) continue;
        if (s.address < min_addr) min_addr = s.address;
        if (s.address > max_addr) max_addr = s.address;
    }
    if (min_addr > max_addr) return;

    uint16_t count = max_addr - min_addr + 1;
    if (count > MODBUS_MAX_REGISTERS) {
        ESP_LOGE("modbus_device", "Register range %u too large for one request", count);
        return;
    }

    uint16_t buf[MODBUS_MAX_REGISTERS] = {};
    esp_err_t err = input
        ? self->read_input_registers(min_addr, count, buf)
        : self->read_holding_registers(min_addr, count, buf);
    if (err != ESP_OK) return;

    for (const auto &s : specs) {
        if (s.input != input) continue;
        RegisterReading r;
        r.address = s.address;
        r.value   = buf[s.address - min_addr];
        out.push_back(r);
    }
}

void ModbusDevice::poll_task(void *arg)
{
    ModbusDevice *self = static_cast<ModbusDevice *>(arg);

    while (true)
    {
        if (self->m_paused)
        {
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        for(RegisterSpec r: self->m_regs) {
            ESP_LOGI(TAG, "[%s] fetching [%s] register at address [%u]...",
                     self->m_config.id, r.input ? "input" : "holding", r.address);
        }

        std::vector<RegisterReading> readings;
        read_group(self, self->m_regs, true,  readings); // input registers
        read_group(self, self->m_regs, false, readings); // holding registers

        if (!readings.empty() && self->m_readings_cb)
        {
            ESP_LOGI(TAG, "[%s] sending readings for [%zu] registers...", self->m_config.id, readings.size());
            self->m_readings_cb(readings, self->m_readings_cb_arg);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void ModbusDevice::close_socket()
{
    if (m_sock >= 0)
    {
        close(m_sock);
        m_sock = -1;
    }
    m_state = ModbusConnectionState::Disconnected;
}

esp_err_t ModbusDevice::ensure_connected()
{
    if (m_sock >= 0)
        return ESP_OK;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", m_config.port);

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (getaddrinfo(m_config.host, port_str, &hints, &res) != 0 || res == nullptr)
    {
        ESP_LOGE(TAG, "[%s] DNS lookup failed for %s", m_config.id, m_config.host);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "[%s] socket() failed: %d", m_config.id, errno);
        freeaddrinfo(res);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    // Non-blocking connect with a bounded wait, so a silently-dropped SYN to an
    // unreachable host times out in a few seconds rather than the TCP default.
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(sock, res->ai_addr, res->ai_addrlen);
    if (rc != 0 && errno == EINPROGRESS)
    {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        struct timeval tv = { .tv_sec = MODBUS_CONNECT_TIMEOUT_S, .tv_usec = 0 };
        rc = select(sock + 1, nullptr, &wset, nullptr, &tv);
        if (rc > 0)
        {
            int soerr = 0;
            socklen_t slen = sizeof(soerr);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &slen);
            errno = soerr;
            rc = (soerr == 0) ? 0 : -1;
        }
        else
        {
            rc = -1; // select() timed out (0) or errored (<0)
        }
    }
    if (rc != 0)
    {
        ESP_LOGE(TAG, "[%s] connect(%s:%u) failed: %d", m_config.id, m_config.host, m_config.port, errno);
        freeaddrinfo(res);
        close(sock);
        m_state = ModbusConnectionState::Failed;
        return ESP_FAIL;
    }

    // Restore blocking mode and bound send/recv so a half-open peer can't stall us.
    fcntl(sock, F_SETFL, flags);
    struct timeval io_to = { .tv_sec = MODBUS_IO_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &io_to, sizeof(io_to));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &io_to, sizeof(io_to));

    freeaddrinfo(res);
    m_sock = sock;
    m_state = ModbusConnectionState::Connected;
    ESP_LOGI(TAG, "[%s] connected to %s:%u", m_config.id, m_config.host, m_config.port);
    return ESP_OK;
}

esp_err_t ModbusDevice::send_request(uint8_t func_code, uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    if (count == 0 || count > MODBUS_MAX_REGISTERS)
        return ESP_ERR_INVALID_ARG;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        if (attempt > 0)
        {
            ESP_LOGW(TAG, "[%s] retrying after 1s", m_config.id);
            vTaskDelay(pdMS_TO_TICKS(1000 * attempt));
        }

        if (ensure_connected() != ESP_OK)
            return ESP_FAIL;

        uint16_t tid = ++m_transaction_id;

        // MBAP header (6 bytes) + Unit ID + FC + Start addr (2) + Count (2)
        uint8_t req[12];
        req[0] = tid >> 8;
        req[1] = tid & 0xFF;
        req[2] = 0;
        req[3] = 0;
        req[4] = 0;
        req[5] = 6;
        req[6] = m_config.unit_id;
        req[7] = func_code;
        req[8] = reg_addr >> 8;
        req[9] = reg_addr & 0xFF;
        req[10] = count >> 8;
        req[11] = count & 0xFF;

        int sent = 0;
        bool send_ok = true;
        while (sent < (int)sizeof(req))
        {
            int n = send(m_sock, req + sent, sizeof(req) - sent, 0);
            if (n <= 0)
            {
                ESP_LOGE(TAG, "[%s] send failed: %d", m_config.id, errno);
                close_socket();
                send_ok = false;
                break;
            }
            sent += n;
        }
        if (!send_ok)
            continue;

        // Read fixed 9-byte response prefix: 6 MBAP + unit_id + FC + byte_count/exception_code
        uint8_t header[9];
        int received = 0;
        bool recv_ok = true;
        while (received < (int)sizeof(header))
        {
            int n = recv(m_sock, header + received, sizeof(header) - received, 0);
            if (n <= 0)
            {
                ESP_LOGE(TAG, "[%s] recv header failed: %d", m_config.id, errno);
                close_socket();
                recv_ok = false;
                break;
            }
            received += n;
        }
        if (!recv_ok)
            continue;

        if (header[7] & 0x80)
        {
            ESP_LOGE(TAG, "[%s] Modbus exception: FC=0x%02x code=%u", m_config.id, header[7], header[8]);
            return ESP_FAIL;
        }

        int data_len = header[8];
        uint8_t data[MODBUS_MAX_REGISTERS * 2];
        received = 0;
        bool data_ok = true;
        while (received < data_len)
        {
            int n = recv(m_sock, data + received, data_len - received, 0);
            if (n <= 0)
            {
                ESP_LOGE(TAG, "[%s] recv data failed: %d", m_config.id, errno);
                close_socket();
                data_ok = false;
                break;
            }
            received += n;
        }
        if (!data_ok)
            continue;

        for (int i = 0; i < (int)count; i++)
        {
            out[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
        }

        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t ModbusDevice::read_input_registers(uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    return send_request(0x04, reg_addr, count, out);
}

esp_err_t ModbusDevice::read_holding_registers(uint16_t reg_addr, uint16_t count, uint16_t *out)
{
    return send_request(0x03, reg_addr, count, out);
}
