#include "modbus_manager.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_eth.h"

#include "cJSON.h"

static const char *TAG = "modbus_manager";

static void collect_regs_from_endpoint(cJSON *ep, std::vector<RegisterSpec> &regs)
{
    cJSON *mappings = cJSON_GetObjectItemCaseSensitive(ep, "mappings");
    if (!cJSON_IsArray(mappings)) return;
    cJSON *m = nullptr;
    cJSON_ArrayForEach(m, mappings) {
        cJSON *func = cJSON_GetObjectItemCaseSensitive(m, "function");
        cJSON *addr = cJSON_GetObjectItemCaseSensitive(m, "address");
        if (!cJSON_IsNumber(func) || !cJSON_IsNumber(addr)) continue;
        regs.push_back({(uint16_t)addr->valueint, func->valueint == 4});
    }
}

static std::vector<RegisterSpec> build_register_specs(const char *matter_structure_json)
{
    std::vector<RegisterSpec> regs;
    if (!matter_structure_json || matter_structure_json[0] == '\0') return regs;

    cJSON *root = cJSON_Parse(matter_structure_json);
    if (!root) return regs;

    cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(root, "endpoints");
    if (cJSON_IsArray(endpoints)) {
        cJSON *ep = nullptr;
        cJSON_ArrayForEach(ep, endpoints) {
            collect_regs_from_endpoint(ep, regs);

            cJSON *parts = cJSON_GetObjectItemCaseSensitive(ep, "parts");
            if (cJSON_IsArray(parts)) {
                cJSON *part = nullptr;
                cJSON_ArrayForEach(part, parts) {
                    collect_regs_from_endpoint(part, regs);
                }
            }
        }
    }
    cJSON_Delete(root);

    // Remove duplicates — same address + function code may appear across root and parts.
    regs.erase(std::remove_if(regs.begin(), regs.end(), [&](const RegisterSpec &a) {
        for (const auto &b : regs) {
            if (&b == &a) break;
            if (b.address == a.address && b.input == a.input) return true;
        }
        return false;
    }), regs.end());

    return regs;
}

ModbusManager &ModbusManager::instance()
{
    static ModbusManager s_instance;
    return s_instance;
}

void ModbusManager::create_device(const device_config_t &config, const std::vector<RegisterSpec> &regs,
                                   ModbusDevice::readings_cb_t cb)
{
    auto *dev = new ModbusDevice(config, regs);
    if (cb) dev->set_readings_callback(cb, const_cast<char *>(dev->id()));
    m_devices.push_back(dev);
    ESP_LOGI(TAG, "Created Modbus device '%s' (%s:%u)", config.id, config.host, config.port);
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        ModbusManager::instance().pause_all();
    } else if (event_id == ETHERNET_EVENT_CONNECTED) {
        ModbusManager::instance().resume_all();
    }
}

void ModbusManager::init()
{
    // We want to react to Ethernet link events to pause/resume polling, so register an event handler.
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, nullptr);
}

void ModbusManager::start_polling()
{
    for (auto *dev : m_devices) {
        dev->start_polling();
    }
    ESP_LOGI(TAG, "Polling started for %zu device(s)", m_devices.size());
}

void ModbusManager::on_device_added(const device_config_t &config, ModbusDevice::readings_cb_t cb)
{
    auto regs = build_register_specs(config.matter_structure_json);

    ESP_LOGI(TAG, "Registers to read:");

    for (auto i: regs) {
        ESP_LOGI(TAG, " - %s register at address %u", i.input ? "Input" : "Holding", i.address);
    }

    create_device(config, regs, cb);
    ESP_LOGI(TAG, "Device added: %s", config.id);
}

void ModbusManager::on_device_removed(const char *id)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](ModbusDevice *d) { return strcmp(d->id(), id) == 0; });
    if (it == m_devices.end()) return;
    delete *it;
    m_devices.erase(it);
    ESP_LOGI(TAG, "Device removed: %s", id);
}

void ModbusManager::pause_all()
{
    for (auto *dev : m_devices) {
        dev->pause();
    }
    ESP_LOGI(TAG, "All Modbus devices paused");
}

void ModbusManager::resume_all()
{
    for (auto *dev : m_devices) {
        dev->resume();
    }
    ESP_LOGI(TAG, "All Modbus devices resumed");
}

void ModbusManager::clear()
{
    for (auto *dev : m_devices) {
        delete dev;
    }
    m_devices.clear();
    ESP_LOGI(TAG, "All Modbus devices cleared");
}

ModbusDevice *ModbusManager::find(const char *id) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [id](ModbusDevice *d) { return strcmp(d->id(), id) == 0; });
    return it != m_devices.end() ? *it : nullptr;
}

// Build a throwaway ModbusDevice for ad-hoc reads. The config is heap-allocated
// (it embeds the 2 KB matter_structure buffer) to keep the caller's stack small,
// and the device itself lives on the heap so callers must delete it.
static ModbusDevice *make_transient(const char *host, uint16_t port, uint8_t unit_id)
{
    device_config_t *cfg = (device_config_t *)calloc(1, sizeof(device_config_t));
    if (!cfg) return nullptr;
    strlcpy(cfg->id, "transient", sizeof(cfg->id));
    strlcpy(cfg->host, host, sizeof(cfg->host));
    cfg->port    = port;
    cfg->unit_id = unit_id;
    ModbusDevice *dev = new ModbusDevice(*cfg, {});
    free(cfg);
    return dev;
}

esp_err_t ModbusManager::test_connection(const char *host, uint16_t port, uint8_t unit_id)
{
    ModbusDevice *dev = make_transient(host, port, unit_id);
    if (!dev) return ESP_ERR_NO_MEM;

    uint16_t tmp = 0;
    esp_err_t err = dev->read_input_registers(0x0000, 1, &tmp);
    delete dev; // destructor closes the socket
    return err;
}

esp_err_t ModbusManager::read_registers(const char *host, uint16_t port, uint8_t unit_id,
                                        const std::vector<ReadReq> &reqs,
                                        std::vector<ReadResp> &out)
{
    ModbusDevice *dev = make_transient(host, port, unit_id);
    if (!dev) return ESP_ERR_NO_MEM;

    bool any_ok = false;
    for (const auto &r : reqs) {
        uint16_t v = 0;
        esp_err_t err = r.input ? dev->read_input_registers(r.address, 1, &v)
                                : dev->read_holding_registers(r.address, 1, &v);
        bool ok = (err == ESP_OK);
        if (ok) any_ok = true;
        out.push_back({r.address, r.input, ok, v});

        // If we couldn't even open the socket, the host is unreachable — bail out
        // instead of repeating the (slow) connect attempt for every register.
        if (!ok && dev->state() == ModbusConnectionState::Failed) break;
    }

    delete dev;
    return (reqs.empty() || any_ok) ? ESP_OK : ESP_FAIL;
}


