#include "ota_server.h"

#include <string.h>
#include <time.h>
#include <sys/param.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ─── External symbols (provided by each puzzle's main component) ──────────────
extern int  puzzle_get_battery_pct(void);
extern int  puzzle_get_espnow_peer_count(void);

static const char* TAG = "ota_server";

// ─── Module state ─────────────────────────────────────────────────────────────
static httpd_handle_t  s_server       = NULL;
static ota_status_t    s_status       = { .state = OTA_STATE_IDLE };
static void          (*s_complete_cb)(bool) = NULL;
static esp_timer_handle_t s_watchdog  = NULL;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static void json_str(char* buf, size_t size, const char* key, const char* val, bool comma) {
    snprintf(buf + strlen(buf), size - strlen(buf),
             "\"%s\":\"%s\"%s", key, val, comma ? "," : "");
}

static void json_int(char* buf, size_t size, const char* key, int val, bool comma) {
    snprintf(buf + strlen(buf), size - strlen(buf),
             "\"%s\":%d%s", key, val, comma ? "," : "");
}

// ─── GET /version ─────────────────────────────────────────────────────────────

static esp_err_t handle_version(httpd_req_t* req) {
    char buf[256] = "{";
    json_str(buf, sizeof(buf), "firmware", OTA_FIRMWARE_NAME, true);
    json_str(buf, sizeof(buf), "version", OTA_FIRMWARE_VERSION, true);
    json_str(buf, sizeof(buf), "idf", IDF_VER, false);
    strncat(buf, "}", sizeof(buf) - strlen(buf) - 1);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ─── GET /status ──────────────────────────────────────────────────────────────

static esp_err_t handle_status(httpd_req_t* req) {
    char buf[256] = "{";
    json_int(buf, sizeof(buf), "battery_pct",   puzzle_get_battery_pct(), true);
    json_int(buf, sizeof(buf), "uptime_s",      (int)(esp_timer_get_time() / 1000000), true);
    json_int(buf, sizeof(buf), "espnow_peers",  puzzle_get_espnow_peer_count(), true);
    json_int(buf, sizeof(buf), "heap_free",     (int)esp_get_free_heap_size(), false);
    strncat(buf, "}", sizeof(buf) - strlen(buf) - 1);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ─── GET /ota/status ──────────────────────────────────────────────────────────

static const char* state_to_str(ota_state_t state) {
    switch (state) {
        case OTA_STATE_IDLE:        return "idle";
        case OTA_STATE_DOWNLOADING: return "downloading";
        case OTA_STATE_VERIFYING:   return "verifying";
        case OTA_STATE_REBOOTING:   return "rebooting";
        case OTA_STATE_ERROR:       return "error";
        default:                    return "unknown";
    }
}

static esp_err_t handle_ota_status(httpd_req_t* req) {
    char buf[256] = "{";
    json_str(buf, sizeof(buf), "state", state_to_str(s_status.state), true);
    json_int(buf, sizeof(buf), "progress", s_status.progress, false);
    strncat(buf, "}", sizeof(buf) - strlen(buf) - 1);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ─── POST /ota ────────────────────────────────────────────────────────────────

static void do_ota_task(void* arg) {
    esp_ota_handle_t    ota_handle    = 0;
    const esp_partition_t* ota_part   = NULL;
    httpd_req_t*        req           = (httpd_req_t*)arg;
    esp_err_t           err           = ESP_OK;
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);

    uint8_t*  buf   = malloc(OTA_CHUNK_SIZE);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); goto cleanup; }

    // Check content length
    int total = req->content_len;
    if (total <= 0 || total > OTA_MAX_UPLOAD_SIZE) {
        ESP_LOGE(TAG, "Invalid content length: %d", total);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid size");
        goto cleanup;
    }

    // Get OTA partition
    ota_part = esp_ota_get_next_update_partition(NULL);
    if (!ota_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        goto cleanup;
    }

    err = esp_ota_begin(ota_part, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        goto cleanup;
    }

    s_status.state         = OTA_STATE_DOWNLOADING;
    s_status.bytes_received = 0;
    s_status.total_bytes   = total;
    s_status.progress      = 0;

    // SHA256 context for integrity check
    mbedtls_sha256_starts(&sha_ctx, 0);

    // Receive and write firmware chunks
    int received = 0;
    while (received < total) {
        int chunk_size = MIN(OTA_CHUNK_SIZE, total - received);
        int r = httpd_req_recv(req, (char*)buf, chunk_size);

        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Recv error: %d", r);
            err = ESP_FAIL;
            break;
        }

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at offset %d: %s", received, esp_err_to_name(err));
            break;
        }

        mbedtls_sha256_update(&sha_ctx, buf, r);
        received += r;
        s_status.bytes_received = received;
        s_status.progress = (received * 100) / total;
    }

    if (err != ESP_OK) {
        snprintf(s_status.error, sizeof(s_status.error), "Write failed: %s", esp_err_to_name(err));
        s_status.state = OTA_STATE_ERROR;
        esp_ota_abort(ota_handle);
        ota_handle = 0;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_status.error);
        goto cleanup;
    }

    // Compute SHA256 of received data
    uint8_t sha256[32];
    mbedtls_sha256_finish(&sha_ctx, sha256);
    mbedtls_sha256_free(&sha_ctx);

    s_status.state    = OTA_STATE_VERIFYING;
    s_status.progress = 95;

    // Finalize OTA
    err = esp_ota_end(ota_handle);
    ota_handle = 0;
    if (err != ESP_OK) {
        snprintf(s_status.error, sizeof(s_status.error), "OTA end failed: %s", esp_err_to_name(err));
        s_status.state = OTA_STATE_ERROR;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_status.error);
        goto cleanup;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(ota_part);
    if (err != ESP_OK) {
        snprintf(s_status.error, sizeof(s_status.error), "Set boot failed: %s", esp_err_to_name(err));
        s_status.state = OTA_STATE_ERROR;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_status.error);
        goto cleanup;
    }

    s_status.progress = 100;
    s_status.state    = OTA_STATE_REBOOTING;

    // Respond before reboot
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Firmware accepted, rebooting\"}");

    if (s_complete_cb) s_complete_cb(true);

    ESP_LOGI(TAG, "OTA success, rebooting in 1s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

cleanup:
    if (ota_handle) esp_ota_abort(ota_handle);
    free(buf);
    mbedtls_sha256_free(&sha_ctx);
    vTaskDelete(NULL);
}

static esp_err_t handle_ota_upload(httpd_req_t* req) {
    // Rate limiting
    int64_t now = esp_timer_get_time() / 1000000;
    if (s_status.last_ota_time > 0 && (now - s_status.last_ota_time) < OTA_RATE_LIMIT_SECS) {
        httpd_resp_send_err(req, HTTPD_429_TOO_MANY_REQUESTS, "Rate limited: wait 60s");
        return ESP_FAIL;
    }

    if (s_status.state != OTA_STATE_IDLE) {
        httpd_resp_send_err(req, HTTPD_409_CONFLICT, "OTA already in progress");
        return ESP_FAIL;
    }

    s_status.last_ota_time = now;

    // Run OTA in a separate task to not block the HTTP server
    if (xTaskCreate(do_ota_task, "ota_task", 8192, req, 5, NULL) != pdPASS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task create failed");
        return ESP_FAIL;
    }

    // Task will send the HTTP response
    return ESP_OK;
}

// ─── POST /ota/rollback ───────────────────────────────────────────────────────

static esp_err_t handle_ota_rollback(httpd_req_t* req) {
    const esp_partition_t* prev = esp_ota_get_last_invalid_partition();
    if (!prev) {
        // Try running partition as fallback
        prev = esp_ota_get_running_partition();
    }

    if (!prev) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No previous partition to roll back to");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_set_boot_partition(prev);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Rollback failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rolling back, rebooting\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ─── Watchdog (auto-rollback) ──────────────────────────────────────────────────

static void watchdog_cb(void* arg) {
    ESP_LOGE(TAG, "Watchdog expired -- new firmware did not call ota_server_mark_valid(), rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

static void start_watchdog(void) {
    const esp_timer_create_args_t args = {
        .callback = watchdog_cb,
        .name     = "ota_watchdog",
    };
    esp_timer_create(&args, &s_watchdog);
    esp_timer_start_once(s_watchdog, (int64_t)OTA_WATCHDOG_SECS * 1000000);
    ESP_LOGI(TAG, "OTA watchdog started (%ds to mark valid)", OTA_WATCHDOG_SECS);
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t ota_server_init(void) {
    // Check if we booted from an OTA partition that needs validation
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Running unvalidated OTA firmware -- starting watchdog");
            start_watchdog();
        }
    }

    // HTTP server config
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port     = OTA_SERVER_PORT;
    config.max_uri_handlers = 8;
    config.uri_match_fn    = httpd_uri_match_wildcard;
    config.stack_size      = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    // Register URI handlers
    static const httpd_uri_t uris[] = {
        { .uri = "/version",      .method = HTTP_GET,  .handler = handle_version      },
        { .uri = "/status",       .method = HTTP_GET,  .handler = handle_status       },
        { .uri = "/ota",          .method = HTTP_POST, .handler = handle_ota_upload   },
        { .uri = "/ota/status",   .method = HTTP_GET,  .handler = handle_ota_status   },
        { .uri = "/ota/rollback", .method = HTTP_POST, .handler = handle_ota_rollback },
    };

    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI %s: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "OTA server started on port %d (%s v%s)",
             OTA_SERVER_PORT, OTA_FIRMWARE_NAME, OTA_FIRMWARE_VERSION);

    return ESP_OK;
}

void ota_server_mark_valid(void) {
    if (s_watchdog) {
        esp_timer_stop(s_watchdog);
        esp_timer_delete(s_watchdog);
        s_watchdog = NULL;
        ESP_LOGI(TAG, "OTA watchdog cancelled -- firmware marked valid");
    }
    esp_ota_mark_app_valid_cancel_rollback();
}

const ota_status_t* ota_server_get_status(void) {
    return &s_status;
}

void ota_server_set_complete_cb(void (*cb)(bool success)) {
    s_complete_cb = cb;
}
