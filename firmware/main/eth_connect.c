/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define ETH_SPI_HOST SPI2_HOST
#define ETH_SPI_SCLK_GPIO 13
#define ETH_SPI_MOSI_GPIO 11
#define ETH_SPI_MISO_GPIO 12
#define ETH_SPI_CS_GPIO 14
#define ETH_SPI_INT_GPIO 10
#define ETH_SPI_RST_GPIO 9
#define ETH_SPI_CLOCK_MHZ 25

static esp_netif_t *eth_start(void);

static const char *TAG = "ethernet_connect";

static void on_eth_event(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        ESP_ERROR_CHECK(esp_netif_create_ip6_linklocal((esp_netif_t *)esp_netif));
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

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

static void on_ipv6_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    ESP_LOGI(TAG, "Got IPv6: " IPV6STR, IPV62STR(event->ip6_info.ip));
}

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

static esp_netif_t *eth_start(void)
{
    gpio_install_isr_service(0);

    ESP_ERROR_CHECK(esp_netif_init());                                                                                                                                                                     
    ESP_ERROR_CHECK(esp_event_loop_create_default());     

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();                                                                                                                                         
    esp_netif_t *eth_netif = esp_netif_new(&netif_config);
    
    spi_bus_config_t buscfg = {
        .miso_io_num = ETH_SPI_MISO_GPIO,
        .mosi_io_num = ETH_SPI_MOSI_GPIO,
        .sclk_io_num = ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = ETH_SPI_CS_GPIO,
        .queue_size = 20
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = ETH_SPI_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();                                                                                                                                            
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();                                                                                                                                            
    phy_config.reset_gpio_num = ETH_SPI_RST_GPIO;
    s_phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));

    uint8_t eth_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(eth_mac, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac));

    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    esp_netif_attach(eth_netif, s_eth_glue);

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_ipv6_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, eth_netif));

    esp_eth_start(s_eth_handle);

    return eth_netif;
}

esp_err_t eth_connect(void)
{
    eth_start();
    return ESP_OK;
}