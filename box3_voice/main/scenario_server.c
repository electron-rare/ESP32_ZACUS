// scenario_server.c — minimal HTTP server for receiving Runtime 3 IR scenarios
// on the ESP32-S3-BOX-3. Mirrors the master's game_endpoint handler but is
// self-contained (no shared component) since box3_voice is a separate IDF
// project. Storage uses the existing SPIFFS partition declared in
// partitions.csv (master uses LittleFS — both work, we match the local table).

#include "scenario_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "scenario_srv"

#define MAX_SCENARIO_BYTES (64 * 1024)
#define SPIFFS_LABEL       "storage"
#define SPIFFS_BASE        "/spiffs"
#define SCENARIO_PATH      SPIFFS_BASE "/scenario.json"
#define SCENARIO_BAK       SPIFFS_BASE "/scenario.bak"

static httpd_handle_t s_server = NULL;
static bool s_spiffs_mounted = false;

// ---------- helpers ----------

static esp_err_t send_json(httpd_req_t *req, const char *status_line, const char *body) {
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t send_error(httpd_req_t *req, const char *status_line, const char *message) {
    char buf[192];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message ? message : "");
    return send_json(req, status_line, buf);
}

static esp_err_t mount_spiffs_lazy(void) {
    if (s_spiffs_mounted) return ESP_OK;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = SPIFFS_LABEL,
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_spiffs_mounted = true;
        ESP_LOGI(TAG, "spiffs '%s' mounted at %s", conf.partition_label, conf.base_path);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
    return err;
}

static void deferred_restart_task(void *arg) {
    (void) arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGW(TAG, "scenario hot-load: rebooting to apply new IR");
    esp_restart();
}

static void schedule_restart(void) {
    xTaskCreate(deferred_restart_task, "scenario_restart",
                4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}

// ---------- handlers ----------

static esp_err_t handle_healthz_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t handle_scenario_post(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > MAX_SCENARIO_BYTES) {
        ESP_LOGW(TAG, "POST /game/scenario: bad body length %d", (int) req->content_len);
        return send_error(req, "413 Payload Too Large", "body must be 1..65536 bytes");
    }
    if (mount_spiffs_lazy() != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "spiffs mount failed");
    }
    char *body = (char *) malloc((size_t) req->content_len + 1);
    if (!body) return send_error(req, "500 Internal Server Error", "out of memory");
    int total = 0;
    while (total < (int) req->content_len) {
        int got = httpd_req_recv(req, body + total, req->content_len - total);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(body);
            return send_error(req, "400 Bad Request", "recv failed");
        }
        total += got;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        free(body);
        return send_error(req, "400 Bad Request", "malformed json");
    }
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!cJSON_IsString(schema) || strcmp(schema->valuestring, "zacus.runtime3.v1") != 0) {
        cJSON_Delete(root); free(body);
        return send_error(req, "400 Bad Request", "schema_version must be zacus.runtime3.v1");
    }
    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(root, "steps");
    if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) == 0) {
        cJSON_Delete(root); free(body);
        return send_error(req, "400 Bad Request", "steps must be a non-empty array");
    }
    const cJSON *scenario_obj = cJSON_GetObjectItemCaseSensitive(root, "scenario");
    const cJSON *entry = scenario_obj
        ? cJSON_GetObjectItemCaseSensitive(scenario_obj, "entry_step_id") : NULL;
    char entry_str[64] = {0};
    if (cJSON_IsString(entry) && entry->valuestring) {
        strncpy(entry_str, entry->valuestring, sizeof(entry_str) - 1);
    }
    int steps_count = cJSON_GetArraySize(steps);
    cJSON_Delete(root);

    // Rotate current -> .bak
    struct stat st;
    if (stat(SCENARIO_PATH, &st) == 0) {
        unlink(SCENARIO_BAK);
        if (rename(SCENARIO_PATH, SCENARIO_BAK) != 0) {
            ESP_LOGW(TAG, "rename .json -> .bak failed (errno=%d)", errno);
        }
    }

    FILE *f = fopen(SCENARIO_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen %s for write failed (errno=%d)", SCENARIO_PATH, errno);
        free(body);
        return send_error(req, "500 Internal Server Error", "scenario write open failed");
    }
    size_t written = fwrite(body, 1, (size_t) total, f);
    fclose(f);
    free(body);
    if ((int) written != total) {
        unlink(SCENARIO_PATH);
        if (stat(SCENARIO_BAK, &st) == 0) rename(SCENARIO_BAK, SCENARIO_PATH);
        return send_error(req, "500 Internal Server Error", "scenario write short");
    }

    ESP_LOGI(TAG, "scenario hot-load OK: %d bytes, %d steps, entry=%s",
             total, steps_count, entry_str);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"board\":\"box3_voice\",\"steps_count\":%d,"
             "\"entry_step_id\":\"%s\",\"bytes\":%d,\"reload\":\"reboot_pending\"}",
             steps_count, entry_str, total);
    esp_err_t ret = send_json(req, "200 OK", buf);
    schedule_restart();
    return ret;
}

// ---------- public init ----------

esp_err_t scenario_server_start(void) {
    if (s_server) {
        ESP_LOGW(TAG, "scenario_server already running");
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uri_healthz = {
        .uri = "/healthz", .method = HTTP_GET,
        .handler = handle_healthz_get, .user_ctx = NULL,
    };
    static const httpd_uri_t uri_scenario = {
        .uri = "/game/scenario", .method = HTTP_POST,
        .handler = handle_scenario_post, .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_healthz);
    httpd_register_uri_handler(s_server, &uri_scenario);

    ESP_LOGI(TAG, "scenario server up on :80 (GET /healthz, POST /game/scenario)");
    return ESP_OK;
}
