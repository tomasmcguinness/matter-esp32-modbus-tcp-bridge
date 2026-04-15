#pragma once
#include "esp_err.h"
#include "esp_netif.h"

typedef void (*eth_got_ip_cb_t)(esp_netif_t *netif);

esp_err_t eth_connect(eth_got_ip_cb_t on_got_ip);
