#include "devices_store.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_littlefs.h"
#include "cJSON.h"

static const char *TAG = "devices_store";

#define LFS_BASE_PATH    "/littlefs"
#define LFS_LABEL        "config"
#define DEVICES_PATH     LFS_BASE_PATH "/devices.json"
#define DEVICES_TMP_PATH LFS_BASE_PATH "/devices.json.tmp"

static device_config_t s_devices[DEVICES_MAX];
static size_t s_count = 0;

static void generate_id(char out[DEVICE_ID_LEN])
{
    uint8_t bytes[8];
    esp_fill_random(bytes, sizeof(bytes));
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i)
    {
        out[i * 2]     = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[DEVICE_ID_LEN - 1] = '\0';
}

static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = LFS_BASE_PATH,
        .partition_label        = LFS_LABEL,
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LittleFS mount failed: 0x%x", err);
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_littlefs_info(LFS_LABEL, &total, &used) == ESP_OK)
    {
        ESP_LOGI(TAG, "LittleFS mounted at %s (used %u / %u bytes)", LFS_BASE_PATH, used, total);
    }
    return ESP_OK;
}

static void load_from_disk(void)
{
    FILE *f = fopen(DEVICES_PATH, "r");
    if (!f)
    {
        ESP_LOGI(TAG, "No devices file yet");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16384)
    {
        ESP_LOGW(TAG, "devices.json unexpected size %ld; ignoring", size);
        fclose(f);
        return;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf)
    {
        fclose(f);
        return;
    }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsArray(root))
    {
        ESP_LOGW(TAG, "devices.json parse failed");
        cJSON_Delete(root);
        return;
    }

    s_count = 0;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (s_count >= DEVICES_MAX) break;
        cJSON *id     = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *name   = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON *host   = cJSON_GetObjectItemCaseSensitive(item, "host");
        cJSON *port   = cJSON_GetObjectItemCaseSensitive(item, "port");
        cJSON *unitId = cJSON_GetObjectItemCaseSensitive(item, "unitId");
        if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsString(host) ||
            !cJSON_IsNumber(port) || !cJSON_IsNumber(unitId))
        {
            continue;
        }
        device_config_t *d = &s_devices[s_count++];
        strlcpy(d->id,   id->valuestring,   sizeof(d->id));
        strlcpy(d->name, name->valuestring, sizeof(d->name));
        strlcpy(d->host, host->valuestring, sizeof(d->host));
        d->port    = (uint16_t)port->valueint;
        d->unit_id = (uint8_t)unitId->valueint;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %u devices", (unsigned)s_count);
}

static esp_err_t persist(void)
{
    cJSON *root = cJSON_CreateArray();
    if (!root) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < s_count; ++i)
    {
        const device_config_t *d = &s_devices[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id",     d->id);
        cJSON_AddStringToObject(o, "name",   d->name);
        cJSON_AddStringToObject(o, "host",   d->host);
        cJSON_AddNumberToObject(o, "port",   d->port);
        cJSON_AddNumberToObject(o, "unitId", d->unit_id);
        cJSON_AddItemToArray(root, o);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return ESP_ERR_NO_MEM;

    esp_err_t result = ESP_OK;
    FILE *f = fopen(DEVICES_TMP_PATH, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "fopen tmp failed");
        result = ESP_FAIL;
        goto done;
    }
    if (fputs(text, f) == EOF)
    {
        ESP_LOGE(TAG, "fputs failed");
        fclose(f);
        result = ESP_FAIL;
        goto done;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(DEVICES_TMP_PATH, DEVICES_PATH) != 0)
    {
        ESP_LOGE(TAG, "rename failed");
        result = ESP_FAIL;
    }

done:
    free(text);
    return result;
}

esp_err_t devices_store_init(void)
{
    esp_err_t err = mount_littlefs();
    if (err != ESP_OK) return err;
    load_from_disk();
    return ESP_OK;
}

size_t devices_store_count(void)
{
    return s_count;
}

const device_config_t *devices_store_at(size_t index)
{
    return index < s_count ? &s_devices[index] : nullptr;
}

esp_err_t devices_store_add(const char *name,
                            const char *host,
                            uint16_t port,
                            uint8_t unit_id,
                            device_config_t *out)
{
    if (!name || !host) return ESP_ERR_INVALID_ARG;
    if (s_count >= DEVICES_MAX) return ESP_ERR_NO_MEM;

    device_config_t *d = &s_devices[s_count];
    generate_id(d->id);
    strlcpy(d->name, name, sizeof(d->name));
    strlcpy(d->host, host, sizeof(d->host));
    d->port    = port;
    d->unit_id = unit_id;

    s_count++;
    esp_err_t err = persist();
    if (err != ESP_OK)
    {
        s_count--;
        return err;
    }
    if (out) *out = *d;
    return ESP_OK;
}

esp_err_t devices_store_update(const char *id,
                               const char *name,
                               const char *host,
                               uint16_t port,
                               uint8_t unit_id,
                               device_config_t *out)
{
    if (!id || !name || !host) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < s_count; ++i)
    {
        if (strcmp(s_devices[i].id, id) != 0) continue;
        device_config_t backup = s_devices[i];
        device_config_t *d = &s_devices[i];
        strlcpy(d->name, name, sizeof(d->name));
        strlcpy(d->host, host, sizeof(d->host));
        d->port    = port;
        d->unit_id = unit_id;
        esp_err_t err = persist();
        if (err != ESP_OK)
        {
            s_devices[i] = backup;
            return err;
        }
        if (out) *out = *d;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t devices_store_remove(const char *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < s_count; ++i)
    {
        if (strcmp(s_devices[i].id, id) != 0) continue;
        for (size_t j = i; j + 1 < s_count; ++j)
        {
            s_devices[j] = s_devices[j + 1];
        }
        s_count--;
        return persist();
    }
    return ESP_ERR_NOT_FOUND;
}
