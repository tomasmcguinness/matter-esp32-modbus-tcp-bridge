#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*modbus_register_cb_t)(uint16_t value);

esp_err_t modbus_tcp_connect(esp_netif_t *netif, const char *host, uint16_t port);
esp_err_t modbus_tcp_read_holding_registers(uint8_t slave_addr, uint16_t reg_addr, uint16_t count, uint16_t *out_values);
esp_err_t modbus_tcp_read_input_registers(uint8_t slave_addr, uint16_t reg_addr, uint16_t count, uint16_t *out_values);
void modbus_tcp_poll_input_register(uint8_t slave_addr, uint16_t reg_addr, modbus_register_cb_t on_value);

#ifdef __cplusplus
}
#endif
