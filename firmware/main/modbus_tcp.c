#include <stdlib.h>
#include "modbus_tcp.h"
#include "mbcontroller.h"
#include "esp_modbus_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "modbus_tcp"
#define RECONNECT_DELAY_MS 5000

#define STR(fieldname) ((const char*)( fieldname ))
#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }

static void *s_master_handle = NULL;
static esp_netif_t *s_netif = NULL;
static const char *s_host = NULL;
static uint16_t s_port = 0;

static const char *s_ip_table[2];

static const mb_parameter_descriptor_t s_descriptor[] = {
    {
        0,
        STR("Grid Voltage"),
        STR("Volts"),
        1,
        MB_PARAM_INPUT,
        0x0000,
        1,
        0,
        PARAM_TYPE_U16,
        PARAM_SIZE_U16,
        OPTS(0,0,0),
        PAR_PERMS_READ,
    }
};

static esp_err_t do_connect(void)
{
    if (s_master_handle)
    {
        mbc_master_destroy();
        s_master_handle = NULL;
    }

    esp_err_t err = mbc_master_init_tcp(&s_master_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init Modbus master: %s", esp_err_to_name(err));
        return err;
    }

    s_ip_table[0] = s_host;
    s_ip_table[1] = NULL;

    mb_communication_info_t comm_info = {
        .ip_port = s_port,
        .ip_addr_type = MB_IPV4,
        .ip_mode = MB_MODE_TCP,
        .ip_addr = (void *)s_ip_table,
        .ip_netif_ptr = s_netif,
    };

    err = mbc_master_setup(&comm_info);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to setup Modbus master: %s", esp_err_to_name(err));
        return err;
    }

    err = mbc_master_set_descriptor(s_descriptor, sizeof(s_descriptor) / sizeof(s_descriptor[0]));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set descriptor: %s", esp_err_to_name(err));
        return err;
    }

    err = mbc_master_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Modbus master: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Connected to Modbus TCP server %s:%d", s_host, s_port);
    return ESP_OK;
}

static void reconnect_task(void *arg)
{
    while (true)
    {
        esp_err_t err = do_connect();
        if (err == ESP_OK)
        {
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGW(TAG, "Reconnecting in %d ms...", RECONNECT_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}

static void schedule_reconnect(void)
{
    xTaskCreate(reconnect_task, "mb_reconnect", 4096, NULL, 5, NULL);
}

esp_err_t modbus_tcp_connect(esp_netif_t *netif, const char *host, uint16_t port)
{
    s_netif = netif;
    s_host = host;
    s_port = port;

    esp_err_t err = do_connect();
    if (err != ESP_OK)
    {
        schedule_reconnect();
    }
    return err;
}

typedef struct {
    uint8_t slave_addr;
    uint16_t reg_addr;
    modbus_register_cb_t on_value;
} poll_task_args_t;

static void poll_task(void *arg)
{
    poll_task_args_t *args = (poll_task_args_t *)arg;
    while (true) {
        uint16_t value = 0;
        esp_err_t err = modbus_tcp_read_input_registers(args->slave_addr, args->reg_addr, 1, &value);
        if (err == ESP_OK && args->on_value) {
            args->on_value(value);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void modbus_tcp_poll_input_register(uint8_t slave_addr, uint16_t reg_addr, modbus_register_cb_t on_value)
{
    poll_task_args_t *args = malloc(sizeof(poll_task_args_t));
    args->slave_addr = slave_addr;
    args->reg_addr   = reg_addr;
    args->on_value   = on_value;
    xTaskCreate(poll_task, "mb_poll", 4096, args, 5, NULL);
}

static esp_err_t send_request(mb_param_request_t *request, uint16_t *out_values)
{
    esp_err_t err = mbc_master_send_request(request, (void *)out_values);
    if (err == ESP_ERR_TIMEOUT || err == ESP_FAIL)
    {
        ESP_LOGW(TAG, "Request failed (%s), reconnecting...", esp_err_to_name(err));
        schedule_reconnect();
    }
    return err;
}

esp_err_t modbus_tcp_read_holding_registers(uint8_t slave_addr, uint16_t reg_addr, uint16_t count, uint16_t *out_values)
{
    mb_param_request_t request = {
        .slave_addr = slave_addr,
        .command = 0x03,
        .reg_start = reg_addr,
        .reg_size = count,
    };
    esp_err_t err = send_request(&request, out_values);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read holding registers: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t modbus_tcp_read_input_registers(uint8_t slave_addr, uint16_t reg_addr, uint16_t count, uint16_t *out_values)
{
    mb_param_request_t request = {
        .slave_addr = slave_addr,
        .command = 0x04,
        .reg_start = reg_addr,
        .reg_size = count,
    };
    esp_err_t err = send_request(&request, out_values);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read input registers: %s", esp_err_to_name(err));
    }
    return err;
}
