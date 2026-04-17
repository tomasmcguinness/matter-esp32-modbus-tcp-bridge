#include "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web_server";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static esp_err_t index_get_handler(httpd_req_t *req)
{
    const size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, reinterpret_cast<const char *>(index_html_start), len);
}

esp_err_t web_server_start(void)
{
    httpd_handle_t server   = nullptr;
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    const httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &index_uri);

    ESP_LOGI(TAG, "Web server listening on port %d", config.server_port);
    return ESP_OK;
}
