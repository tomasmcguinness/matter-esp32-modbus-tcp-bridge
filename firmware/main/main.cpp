#include <stdio.h>
#include <nvs_flash.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "modbus_tcp.h"
#include "mdns.h"
#include "web_server.h"
#include "devices_store.h"

#include <esp_matter.h>

#define MDNS_DELEGATED_HOSTNAME "modbus-adapter"
#define MDNS_HTTP_INSTANCE      "modbus-adapter"
#define MDNS_HTTP_PORT          80

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

//static uint16_t electrical_sensor_endpoint_id = 0;

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
    case chip::DeviceLayer::DeviceEventType::kInternetConnectivityChange:
        ESP_LOGI(TAG, "Internet connectivity change");
        break;
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
    {
        ESP_LOGI(TAG, "kInterfaceIpAddressChanged");

        static bool mdns_registered = false;
        if (mdns_registered)
        {
            break;
        }

        esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth_netif == nullptr)
        {
            ESP_LOGE(TAG, "Ethernet netif not found; skipping mDNS delegation");
            break;
        }

        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(eth_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0)
        {
            ESP_LOGW(TAG, "Ethernet netif has no IPv4 yet; skipping mDNS delegation");
            break;
        }

        mdns_ip_addr_t addr = {};
        addr.addr.type       = ESP_IPADDR_TYPE_V4;
        addr.addr.u_addr.ip4 = ip_info.ip;
        addr.next            = nullptr;

        esp_err_t err = mdns_delegate_hostname_add(MDNS_DELEGATED_HOSTNAME, &addr);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "mdns_delegate_hostname_add failed: 0x%x", err);
            break;
        }

        ESP_LOGI(TAG, "mDNS delegated hostname added: %s.local -> " IPSTR, MDNS_DELEGATED_HOSTNAME, IP2STR(&ip_info.ip));

        err = mdns_service_add_for_host(NULL, "_http", "_tcp", MDNS_DELEGATED_HOSTNAME, MDNS_HTTP_PORT, NULL, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "mdns_service_add_for_host failed: 0x%x", err);
            break;
        }

        mdns_registered = true;
        ESP_LOGI(TAG, "mDNS: %s.local -> " IPSTR " with _http._tcp:%d", MDNS_DELEGATED_HOSTNAME, IP2STR(&ip_info.ip), MDNS_HTTP_PORT);
        break;
    }
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

extern "C" void app_main()
{
    nvs_flash_init();

    // Create the root endpoint
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    temperature_sensor::config_t temp_sensor_config;
    endpoint_t * temp_sensor_ep = temperature_sensor::create(node, &temp_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));

    esp_err_t err = esp_matter::start(app_event_cb);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Matter: %d", err);
        return;
    }

    devices_store_init();
    web_server_start();
}