#include <stdio.h>
#include "esp_log.h"
#include "eth_connect.h"
#include "modbus_tcp.h"

#define TAG "main"

static void on_grid_voltage(uint16_t value)
{
    ESP_LOGI(TAG, "Grid Voltage: %u", value);
}

static void on_got_ip(esp_netif_t *netif)
{
    esp_err_t err = modbus_tcp_connect(netif, "192.168.1.164", 502);
    if (err != ESP_OK) {
        return;
    }

    modbus_tcp_poll_input_register(1, 0x0000, on_grid_voltage);
}

void app_main(void)
{
    eth_connect(on_got_ip);
}
