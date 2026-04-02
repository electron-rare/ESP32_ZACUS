// audio_kit_client.cpp - HTTP client for Audio Kit ESP32 telephone module.
// BOX-3 (this device) sends JSON commands to Audio Kit over LAN.

#include "npc/audio_kit_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "AUDIO_KIT";
static char s_base_url[128] = {0};
static bool s_initialized = false;

esp_err_t audio_kit_client_init(const char *base_url)
{
    if (!base_url) return ESP_ERR_INVALID_ARG;
    strncpy(s_base_url, base_url, sizeof(s_base_url) - 1);
    s_initialized = true;
    ESP_LOGI(TAG, "Audio Kit client initialized: %s", s_base_url);
    return ESP_OK;
}

static esp_err_t post_json(const char *path, const char *json_body)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s failed: %s", path, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "POST %s returned %d", path, status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_kit_play_tts(const char *text, const char *tts_url, const char *voice)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "source", "tts");
    cJSON_AddStringToObject(json, "url", tts_url);
    cJSON_AddStringToObject(json, "text", text);
    cJSON_AddStringToObject(json, "voice", voice);
    char *body = cJSON_PrintUnformatted(json);
    esp_err_t err = post_json("/play", body);
    free(body);
    cJSON_Delete(json);
    return err;
}

esp_err_t audio_kit_play_sd(const char *sd_path)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "source", "sd");
    cJSON_AddStringToObject(json, "path", sd_path);
    char *body = cJSON_PrintUnformatted(json);
    esp_err_t err = post_json("/play", body);
    free(body);
    cJSON_Delete(json);
    return err;
}

esp_err_t audio_kit_stop(void)
{
    return post_json("/stop", "{}");
}

esp_err_t audio_kit_ring(const char *pattern)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "pattern", pattern);
    char *body = cJSON_PrintUnformatted(json);
    esp_err_t err = post_json("/ring", body);
    free(body);
    cJSON_Delete(json);
    return err;
}

esp_err_t audio_kit_poll_status(audio_kit_status_t *status)
{
    if (!s_initialized || !status) return ESP_ERR_INVALID_STATE;

    char url[256];
    snprintf(url, sizeof(url), "%s/status", s_base_url);

    char buf[256] = {0};
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 2000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int len = esp_http_client_read(client, buf, sizeof(buf) - 1);
        if (len > 0) {
            cJSON *json = cJSON_Parse(buf);
            if (json) {
                status->playing = cJSON_IsTrue(cJSON_GetObjectItem(json, "playing"));
                status->phone_off_hook = cJSON_IsTrue(cJSON_GetObjectItem(json, "phone_off_hook"));
                status->button_pressed = cJSON_IsTrue(cJSON_GetObjectItem(json, "button_pressed"));
                cJSON *rssi = cJSON_GetObjectItem(json, "wifi_rssi");
                status->wifi_rssi = rssi ? rssi->valueint : 0;
                cJSON_Delete(json);
            }
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

bool audio_kit_is_healthy(void)
{
    if (!s_initialized) return false;

    char url[256];
    snprintf(url, sizeof(url), "%s/health", s_base_url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 2000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return (err == ESP_OK && status == 200);
}
