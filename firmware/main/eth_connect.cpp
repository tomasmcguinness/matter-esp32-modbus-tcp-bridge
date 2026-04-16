#include <string.h>
#include "esp_event.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"

#include "eth_connect.h"

#define ETH_SPI_HOST      SPI2_HOST
#define ETH_SPI_SCLK_GPIO 13
#define ETH_SPI_MOSI_GPIO 11
#define ETH_SPI_MISO_GPIO 12
#define ETH_SPI_CS_GPIO   14
#define ETH_SPI_INT_GPIO  10
#define ETH_SPI_RST_GPIO  9
#define ETH_SPI_CLOCK_MHZ 25

EthConnect::EthConnect(GotIpCallback on_got_ip)
    : on_got_ip_(on_got_ip)
{
}

esp_err_t EthConnect::connect()
{
    return start();
}

esp_err_t EthConnect::start()
{
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    netif_ = esp_netif_new(&netif_config);

    spi_bus_config_t buscfg = {
        .mosi_io_num   = ETH_SPI_MOSI_GPIO,
        .miso_io_num   = ETH_SPI_MISO_GPIO,
        .sclk_io_num   = ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t spi_devcfg = {
        .mode           = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num   = ETH_SPI_CS_GPIO,
        .queue_size     = 20,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = ETH_SPI_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_ = esp_eth_mac_new_w5500(&w5500_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = ETH_SPI_RST_GPIO;
    phy_ = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac_, phy_);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle_));

    uint8_t eth_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(eth_mac, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle_, ETH_CMD_S_MAC_ADDR, eth_mac));

    glue_ = esp_eth_new_netif_glue(eth_handle_);
    esp_netif_attach(netif_, glue_);

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP, &onIpEvent,   this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,  IP_EVENT_GOT_IP6,    &onIpv6Event, this));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,    &onEthEvent,  this));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle_));
    return ESP_OK;
}

void EthConnect::onEthEvent(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    EthConnect *self = static_cast<EthConnect *>(arg);
    switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet link up");
            //esp_netif_create_ip6_linklocal(self->netif_);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet link down");
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet stopped");
            break;
    }
}

void EthConnect::onIpEvent(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    EthConnect *self = static_cast<EthConnect *>(arg);
    ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(data);
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    if (self->on_got_ip_) {
        self->on_got_ip_(event->esp_netif);
    }
}

void EthConnect::onIpv6Event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    EthConnect *self = static_cast<EthConnect *>(arg);
    ip_event_got_ip6_t *event = static_cast<ip_event_got_ip6_t *>(data);
    ESP_LOGI(TAG, "Got IPv6: " IPV6STR, IPV62STR(event->ip6_info.ip));
    if (self->on_got_ip_) {
        self->on_got_ip_(event->esp_netif);
    }
}
