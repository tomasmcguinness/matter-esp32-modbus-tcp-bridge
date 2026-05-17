#include "web_server.h"

#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "devices_store.h"
#include "device_manager.h"
#include "device_readings.h"

#include <setup_payload/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include "esp_timer.h"

static factory_reset_cb_t s_factory_reset_cb = nullptr;
static volatile int64_t s_window_open_time_us = 0;
static const int COMMISSIONING_WINDOW_SECONDS = 180;

#define WS_MAX_CLIENTS 4
static httpd_handle_t s_ws_hd = nullptr;
static int s_ws_fds[WS_MAX_CLIENTS];
static portMUX_TYPE s_ws_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *TAG = "web_server";

#define SPIFFS_BASE_PATH "/spiffs"
#define SPIFFS_LABEL     "storage"
#define FILE_READ_CHUNK  1024
#define FS_PATH_MAX      544

static const char *mime_type_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
    {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css")  == 0) return "text/css";
    if (strcmp(dot, ".js")   == 0) return "application/javascript";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(dot, ".png")  == 0) return "image/png";
    if (strcmp(dot, ".jpg")  == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
    if (strcmp(dot, ".woff") == 0) return "font/woff";
    if (strcmp(dot, ".woff2")== 0) return "font/woff2";
    return "application/octet-stream";
}

static esp_err_t send_file(httpd_req_t *req, const char *fs_path)
{
    FILE *f = fopen(fs_path, "r");
    if (!f)
    {
        ESP_LOGW(TAG, "File not found: %s", fs_path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_type_for(fs_path));

    char buf[FILE_READ_CHUNK];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK)
        {
            fclose(f);
            ESP_LOGE(TAG, "Send chunk failed for %s", fs_path);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

#define DEVICES_API_PREFIX     "/api/devices"
#define DEVICES_API_PREFIX_LEN 12
#define MAX_POST_BODY          4096

static esp_err_t send_json(httpd_req_t *req, cJSON *root, int status)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    if (status == 201) httpd_resp_set_status(req, "201 Created");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static cJSON *device_to_json(const device_config_t *d)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id",     d->id);
    cJSON_AddStringToObject(o, "name",   d->name);
    cJSON_AddStringToObject(o, "host",   d->host);
    cJSON_AddNumberToObject(o, "port",   d->port);
    cJSON_AddNumberToObject(o, "unitId", d->unit_id);

    if (d->matter_structure_json[0] != '\0') {
        ESP_LOGI(TAG, "Parsing matter_structure_json for device '%s': %s", d->id, d->matter_structure_json);
        cJSON *ms = cJSON_Parse(d->matter_structure_json);
        if (ms) cJSON_AddItemToObject(o, "matter_structure", ms);
    }
    return o;
}

static esp_err_t devices_get_handler(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < devices_store_count(); ++i)
    {
        cJSON_AddItemToArray(arr, device_to_json(devices_store_at(i)));
    }
    return send_json(req, arr, 200);
}

static esp_err_t devices_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling devices POST %s", req->uri);

    if (req->content_len <= 0 || req->content_len > MAX_POST_BODY)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(req->content_len + 1);
    if (!buf)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < req->content_len)
    {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0)
        {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv failed");
            return ESP_FAIL;
        }
        received += r;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }
    cJSON *name             = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *host             = cJSON_GetObjectItemCaseSensitive(root, "host");
    cJSON *port             = cJSON_GetObjectItemCaseSensitive(root, "port");
    cJSON *unitId           = cJSON_GetObjectItemCaseSensitive(root, "unitId");
    cJSON *matter_structure = cJSON_GetObjectItemCaseSensitive(root, "matter_structure");
    if (!cJSON_IsString(name) || !cJSON_IsString(host) ||
        !cJSON_IsNumber(port) || !cJSON_IsNumber(unitId) ||
        !cJSON_IsObject(matter_structure))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid fields");
        return ESP_FAIL;
    }

    char *ms_text = cJSON_PrintUnformatted(matter_structure);
    if (!ms_text)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    device_config_t created;
    esp_err_t err = DeviceManager::instance().add_device(name->valuestring,
                                                         host->valuestring,
                                                         (uint16_t)port->valueint,
                                                         (uint8_t)unitId->valueint,
                                                         ms_text,
                                                         &created);
    free(ms_text);
    cJSON_Delete(root);

    if (err == ESP_ERR_NO_MEM)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Device limit reached");
        return ESP_FAIL;
    }
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to add device");
        return ESP_FAIL;
    }
    return send_json(req, device_to_json(&created), 201);
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_sz)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= buf_sz)
    {
        return ESP_FAIL;
    }
    int received = 0;
    while (received < req->content_len)
    {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) return ESP_FAIL;
        received += r;
    }
    buf[received] = '\0';
    return ESP_OK;
}

static const char *extract_id(const char *uri)
{
    if (strncmp(uri, DEVICES_API_PREFIX "/", DEVICES_API_PREFIX_LEN + 1) != 0)
    {
        return nullptr;
    }
    const char *id = uri + DEVICES_API_PREFIX_LEN + 1;
    if (*id == '\0' || strchr(id, '/') != nullptr) return nullptr;
    return id;
}

static esp_err_t devices_put_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling devices PUT %s", req->uri);

    const char *id = extract_id(req->uri);
    if (!id)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad id");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(MAX_POST_BODY + 1);
    if (!buf)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    if (read_body(req, buf, MAX_POST_BODY + 1) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }
    cJSON *name             = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *host             = cJSON_GetObjectItemCaseSensitive(root, "host");
    cJSON *port             = cJSON_GetObjectItemCaseSensitive(root, "port");
    cJSON *unitId           = cJSON_GetObjectItemCaseSensitive(root, "unitId");
    cJSON *matter_structure = cJSON_GetObjectItemCaseSensitive(root, "matter_structure");
    if (!cJSON_IsString(name) || !cJSON_IsString(host) ||
        !cJSON_IsNumber(port) || !cJSON_IsNumber(unitId) ||
        !cJSON_IsObject(matter_structure))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid fields");
        return ESP_FAIL;
    }

    char *ms_text = cJSON_PrintUnformatted(matter_structure);
    if (!ms_text)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    device_config_t updated;
    esp_err_t err = DeviceManager::instance().update_device(id,
                                                            name->valuestring,
                                                            host->valuestring,
                                                            (uint16_t)port->valueint,
                                                            (uint8_t)unitId->valueint,
                                                            ms_text,
                                                            &updated);
    free(ms_text);
    cJSON_Delete(root);

    if (err == ESP_ERR_NOT_FOUND)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device not found");
        return ESP_FAIL;
    }
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update device");
        return ESP_FAIL;
    }
    return send_json(req, device_to_json(&updated), 200);
}

static esp_err_t devices_delete_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    if (strncmp(uri, DEVICES_API_PREFIX "/", DEVICES_API_PREFIX_LEN + 1) != 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    const char *id = uri + DEVICES_API_PREFIX_LEN + 1;
    if (*id == '\0' || strchr(id, '/') != nullptr)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad id");
        return ESP_FAIL;
    }

    esp_err_t err = devices_store_remove(id);
    if (err == ESP_ERR_NOT_FOUND)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device not found");
        return ESP_FAIL;
    }
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Persist failed");
        return ESP_FAIL;
    }
    //DeviceManager::instance().on_device_removed(id);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t factory_reset_handler(httpd_req_t *req)
{
    DeviceManager::instance().clear();
    devices_store_clear();
    
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, nullptr, 0);

    if (s_factory_reset_cb) {
        s_factory_reset_cb();
    }

    return ESP_OK;
}

static esp_err_t device_readings_handler(httpd_req_t *req)
{
    // URI: /api/devices/<id>/readings
    const char *after_prefix = req->uri + DEVICES_API_PREFIX_LEN + 1;
    const char *slash = strchr(after_prefix, '/');
    if (!slash || strcmp(slash, "/readings") != 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    char id[DEVICE_ID_LEN];
    size_t id_len = (size_t)(slash - after_prefix);
    if (id_len == 0 || id_len >= sizeof(id))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad id");
        return ESP_FAIL;
    }
    memcpy(id, after_prefix, id_len);
    id[id_len] = '\0';

    DeviceReadings readings;
    if (!DeviceManager::instance().get_readings(id, readings))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device not found");
        return ESP_FAIL;
    }

    cJSON *arr = cJSON_CreateArray();
    for (const auto &r : readings) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "endpointId",  r.endpoint_id);
        cJSON_AddNumberToObject(entry, "clusterId",   r.cluster_id);
        cJSON_AddNumberToObject(entry, "attributeId", r.attribute_id);
        cJSON_AddNumberToObject(entry, "value",       (double)r.value);
        cJSON_AddItemToArray(arr, entry);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "readings", arr);

    return send_json(req, root, 200);
}

static esp_err_t matter_pairing_handler(httpd_req_t *req)
{
    char qr_buf[128]  = {};
    char manual_buf[32] = {};
    bool window_open  = false;

    struct FabricEntry {
        chip::FabricIndex index;
        chip::VendorId    vendorId;
        chip::NodeId      nodeId;
        char              label[chip::kFabricLabelMaxLengthInBytes + 1];
    };
    FabricEntry fabric_entries[CHIP_CONFIG_MAX_FABRICS] = {};
    uint8_t fabric_count = 0;

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    window_open = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();

    chip::RendezvousInformationFlags rendezvous(chip::RendezvousInformationFlag::kOnNetwork);
    chip::MutableCharSpan qr_span(qr_buf, sizeof(qr_buf) - 1);
    chip::MutableCharSpan manual_span(manual_buf, sizeof(manual_buf) - 1);
    GetQRCode(qr_span, rendezvous);
    GetManualPairingCode(manual_span, rendezvous);

    for (const auto & fabric : chip::Server::GetInstance().GetFabricTable()) {
        if (fabric_count >= CHIP_CONFIG_MAX_FABRICS) break;
        auto & e = fabric_entries[fabric_count++];
        e.index    = fabric.GetFabricIndex();
        e.vendorId = fabric.GetVendorId();
        e.nodeId   = fabric.GetNodeId();
        auto label = fabric.GetFabricLabel();
        size_t len = label.size() < sizeof(e.label) - 1 ? label.size() : sizeof(e.label) - 1;
        memcpy(e.label, label.data(), len);
        e.label[len] = '\0';
    }

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    int window_seconds_remaining = 0;
    if (window_open && s_window_open_time_us > 0) {
        int64_t elapsed = (esp_timer_get_time() - s_window_open_time_us) / 1000000LL;
        int remaining = COMMISSIONING_WINDOW_SECONDS - (int)elapsed;
        window_seconds_remaining = remaining > 0 ? remaining : 0;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "qrCode", qr_buf);
    cJSON_AddStringToObject(root, "manualPairingCode", manual_buf);
    cJSON_AddNumberToObject(root, "windowSecondsRemaining", window_seconds_remaining);

    cJSON *fabrics_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < fabric_count; i++) {
        const auto & e = fabric_entries[i];
        cJSON *f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "fabricIndex", e.index);
        cJSON_AddNumberToObject(f, "vendorId", static_cast<uint16_t>(e.vendorId));
        cJSON_AddStringToObject(f, "label", e.label);
        char node_id_str[20];
        snprintf(node_id_str, sizeof(node_id_str), "0x%016llX", (unsigned long long)e.nodeId);
        cJSON_AddStringToObject(f, "nodeId", node_id_str);
        cJSON_AddItemToArray(fabrics_arr, f);
    }
    cJSON_AddItemToObject(root, "fabrics", fabrics_arr);

    return send_json(req, root, 200);
}

static esp_err_t matter_open_window_handler(httpd_req_t *req)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        CHIP_ERROR err = chip::Server::GetInstance().GetCommissioningWindowManager()
            .OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(COMMISSIONING_WINDOW_SECONDS));
        if (err != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "OpenBasicCommissioningWindow failed: %" CHIP_ERROR_FORMAT, err.Format());
        } else {
            s_window_open_time_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Commissioning window opened (%ds)", COMMISSIONING_WINDOW_SECONDS);
        }
    }, 0);

    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    char fs_path[FS_PATH_MAX];

    if (strcmp(uri, "/") == 0)
    {
        snprintf(fs_path, sizeof(fs_path), "%s/index.html", SPIFFS_BASE_PATH);
    }
    else
    {
        snprintf(fs_path, sizeof(fs_path), "%s%s", SPIFFS_BASE_PATH, uri);
    }

    struct stat st;
    if (stat(fs_path, &st) != 0)
    {
        // Fall back to index.html for SPA client-side routes
        snprintf(fs_path, sizeof(fs_path), "%s/index.html", SPIFFS_BASE_PATH);
        if (stat(fs_path, &st) != 0)
        {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
            return ESP_FAIL;
        }
    }

    return send_file(req, fs_path);
}

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SPIFFS_BASE_PATH,
        .partition_label        = SPIFFS_LABEL,
        .max_files              = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS mount failed: 0x%x", err);
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(SPIFFS_LABEL, &total, &used) == ESP_OK)
    {
        ESP_LOGI(TAG, "SPIFFS mounted at %s (used %u / %u bytes)", SPIFFS_BASE_PATH, used, total);
    }
    return ESP_OK;
}

static esp_err_t ws_events_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        portENTER_CRITICAL(&s_ws_mux);
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (s_ws_fds[i] < 0) {
                s_ws_fds[i] = fd;
                s_ws_hd = req->handle;
                break;
            }
        }
        portEXIT_CRITICAL(&s_ws_mux);
        ESP_LOGI(TAG, "WS client connected fd=%d", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;
    if (frame.len > 0) {
        uint8_t *buf = (uint8_t *)malloc(frame.len);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(buf);
        }
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        portENTER_CRITICAL(&s_ws_mux);
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (s_ws_fds[i] == fd) { s_ws_fds[i] = -1; break; }
        }
        portEXIT_CRITICAL(&s_ws_mux);
        ESP_LOGI(TAG, "WS client disconnected fd=%d", fd);
    }
    return ESP_OK;
}

void web_server_notify_ws_event(const char *event_json)
{
    if (s_ws_hd == nullptr) return;

    int fds[WS_MAX_CLIENTS];
    portENTER_CRITICAL(&s_ws_mux);
    memcpy(fds, s_ws_fds, sizeof(fds));
    portEXIT_CRITICAL(&s_ws_mux);

    httpd_ws_frame_t frame = {
        .final      = true,
        .fragmented = false,
        .type       = HTTPD_WS_TYPE_TEXT,
        .payload    = (uint8_t *)event_json,
        .len        = strlen(event_json),
    };

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (fds[i] < 0) continue;
        if (httpd_ws_get_fd_info(s_ws_hd, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            portENTER_CRITICAL(&s_ws_mux);
            for (int j = 0; j < WS_MAX_CLIENTS; j++) {
                if (s_ws_fds[j] == fds[i]) { s_ws_fds[j] = -1; break; }
            }
            portEXIT_CRITICAL(&s_ws_mux);
            continue;
        }
        esp_err_t err = httpd_ws_send_frame_async(s_ws_hd, fds[i], &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WS send to fd=%d failed: %s", fds[i], esp_err_to_name(err));
            portENTER_CRITICAL(&s_ws_mux);
            for (int j = 0; j < WS_MAX_CLIENTS; j++) {
                if (s_ws_fds[j] == fds[i]) { s_ws_fds[j] = -1; break; }
            }
            portEXIT_CRITICAL(&s_ws_mux);
        }
    }
}

esp_err_t web_server_start(factory_reset_cb_t on_factory_reset)
{
    s_factory_reset_cb = on_factory_reset;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) s_ws_fds[i] = -1;
    esp_err_t err = mount_spiffs();
    if (err != ESP_OK)
    {
        return err;
    }

    httpd_handle_t server   = nullptr;
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.stack_size       = 8192;

    err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    const httpd_uri_t devices_get_uri = {
        .uri      = "/api/devices",
        .method   = HTTP_GET,
        .handler  = devices_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &devices_get_uri);

    const httpd_uri_t devices_post_uri = {
        .uri      = "/api/devices",
        .method   = HTTP_POST,
        .handler  = devices_post_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &devices_post_uri);

    const httpd_uri_t devices_put_uri = {
        .uri      = "/api/devices/*",
        .method   = HTTP_PUT,
        .handler  = devices_put_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &devices_put_uri);

    const httpd_uri_t devices_delete_uri = {
        .uri      = "/api/devices/*",
        .method   = HTTP_DELETE,
        .handler  = devices_delete_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &devices_delete_uri);

    const httpd_uri_t factory_reset_uri = {
        .uri      = "/api/factory-reset",
        .method   = HTTP_POST,
        .handler  = factory_reset_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &factory_reset_uri);

    const httpd_uri_t device_readings_uri = {
        .uri      = "/api/devices/*",
        .method   = HTTP_GET,
        .handler  = device_readings_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &device_readings_uri);

    const httpd_uri_t matter_events_uri = {
        .uri          = "/api/matter/events",
        .method       = HTTP_GET,
        .handler      = ws_events_handler,
        .user_ctx     = nullptr,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &matter_events_uri);

    const httpd_uri_t matter_pairing_uri = {
        .uri      = "/api/matter/pairing",
        .method   = HTTP_GET,
        .handler  = matter_pairing_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &matter_pairing_uri);

    const httpd_uri_t matter_open_window_uri = {
        .uri      = "/api/matter/open-commissioning-window",
        .method   = HTTP_POST,
        .handler  = matter_open_window_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &matter_open_window_uri);

    const httpd_uri_t static_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &static_uri);

    ESP_LOGI(TAG, "Web server listening on port %d", config.server_port);
    return ESP_OK;
}
