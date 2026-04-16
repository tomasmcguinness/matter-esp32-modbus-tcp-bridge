#include <stdio.h>
#include <nvs_flash.h>
#include "esp_log.h"
#include "eth_connect.h"
#include "modbus_tcp.h"
#include "mdns.h"

#include <esp_matter.h>

#define ABORT_APP_ON_FAILURE(x, ...)               \
    do                                             \
    {                                              \
        if (!(unlikely(x)))                        \
        {                                          \
            __VA_ARGS__;                           \
            vTaskDelay(5000 / portTICK_PERIOD_MS); \
            abort();                               \
        }                                          \
    } while (0)

#define TAG "main"

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;

using namespace chip::app::Clusters;

static uint16_t electrical_sensor_endpoint_id = 0;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        break;
    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized");
        break;
    case chip::DeviceLayer::DeviceEventType::kDnssdInitialized:
        ESP_LOGI(TAG, "Dnssd initialized");
        break;
        //{

        //     esp_err_t err = mdns_service_add_for_host("modbus-adapter.local", "_http", "_tcp", "modbus-adapter", 80, NULL, 0);
        //     if (err != ESP_OK) {
        //         ESP_LOGE(TAG, "Failed to add MDNS service: %d", err);
        //     }
        // }
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "kInterfaceIpAddressChanged");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    return ESP_OK;
}

static void on_grid_voltage(uint16_t value)
{
    ESP_LOGI(TAG, "Grid Voltage: %u", value);
}

extern "C" void app_main()
{
    nvs_flash_init();

    static EthConnect eth([](esp_netif_t *netif) {

        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);

        mdns_ip_addr_t addr4 = {};
        addr4.addr.type = ESP_IPADDR_TYPE_V4;
        addr4.addr.u_addr.ip4.addr = ip_info.ip.addr;

        mdns_delegate_hostname_add("modbus-adapter", &addr4);
        esp_err_t err = mdns_service_add_for_host("modbus-adapter.local", "_http", "_tcp", "modbus-adapter", 80, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add MDNS service: %d", err);
        }

    //     esp_err_t err = modbus_tcp_connect(netif, "192.168.1.164", 502);
    //     if (err != ESP_OK) {
    //         return;
    //     }
    //     modbus_tcp_poll_input_register(1, 0x0000, on_grid_voltage);
    });
    eth.connect();

    // Create the root endpoint
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    electrical_sensor::config_t electrical_sensor_config;
    // Configure the sensor as a node.
    electrical_sensor_config.power_topology.feature_flags = power_topology::feature::node_topology::get_id();

    // Configure AC as the power type.
    electrical_sensor_config.electrical_power_measurement.feature_flags = electrical_power_measurement::feature::alternating_current::get_id();
    // electrical_sensor_config.electrical_power_measurement.delegate = &EPMDelegate;

    endpoint_t *endpoint = electrical_sensor::create(node, &electrical_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create electrical sensor endpoint"));
    electrical_sensor_endpoint_id = endpoint::get_id(endpoint);

    cluster_t *cluster = cluster::get(endpoint, chip::app::Clusters::ElectricalPowerMeasurement::Id);
    ABORT_APP_ON_FAILURE(cluster != nullptr, ESP_LOGE(TAG, "Failed to get EPM cluster from endpoint"));

    electrical_power_measurement::attribute::create_voltage(cluster, 0);
    // electrical_power_measurement::attribute::create_active_current(cluster, 0);
    // electrical_power_measurement::attribute::create_active_power(cluster, 0);

    esp_err_t err = esp_matter::start(app_event_cb);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Matter: %d", err);
        return;
    }
}