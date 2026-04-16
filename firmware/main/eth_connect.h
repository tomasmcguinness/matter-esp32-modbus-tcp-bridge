#pragma once
#include <functional>
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_eth.h"

class EthConnect {
public:
    using GotIpCallback = std::function<void(esp_netif_t *)>;

    explicit EthConnect(GotIpCallback on_got_ip);
    esp_err_t connect();

private:
    esp_err_t start();

    static void onEthEvent(void *arg, esp_event_base_t base, int32_t id, void *data);
    static void onIpEvent(void *arg, esp_event_base_t base, int32_t id, void *data);
    static void onIpv6Event(void *arg, esp_event_base_t base, int32_t id, void *data);

    GotIpCallback on_got_ip_;
    esp_netif_t                  *netif_  = nullptr;
    esp_eth_handle_t              eth_handle_ = nullptr;
    esp_eth_mac_t                *mac_   = nullptr;
    esp_eth_phy_t                *phy_   = nullptr;
    esp_eth_netif_glue_handle_t   glue_  = nullptr;

    static constexpr const char *TAG = "ethernet_connect";
};
