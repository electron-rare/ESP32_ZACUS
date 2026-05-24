// game_endpoint — see include/game_endpoint.h for design notes.
//
// Slice 12 of the IDF migration: live-tunable group profile so the
// game master can switch the hints engine policy from the dashboard
// (or any HTTP client) without reflashing NVS.

#include "game_endpoint.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "hints_client.h"

static const char *TAG = "game_endpoint";

// Whitelist mirrored in the 4xx error message so the operator can
// recover without grepping the source. Keep aligned with
// hints_client_set_group_profile() validation.
#define GAME_ENDPOINT_PROFILE_HELP \
    "invalid group_profile, must be one of [TECH, NON_TECH, MIXED, BOTH]"

// ─── small JSON response helper (mirrors voice_hook_endpoint) ───────────────

static esp_err_t send_json(httpd_req_t *req, const char *status_line,
                           const char *body) {
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t send_error(httpd_req_t *req, const char *status_line,
                            const char *message) {
    char buf[192];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message ? message : "");
    return send_json(req, status_line, buf);
}

// ─── NVS persistence helper ─────────────────────────────────────────────────

// Writes the (already-validated) profile into NVS namespace "zacus",
// key "group_profile". Logs and returns the underlying error on
// failure — the caller decides whether to surface it to the client.
static esp_err_t persist_group_profile(const char *profile) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("zacus", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(zacus, RW): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, "group_profile", profile);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str(group_profile=\"%s\"): %s",
                 profile, esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit(zacus): %s", esp_err_to_name(err));
    }
    nvs_close(h);
    return err;
}

// ─── GET /game/group_profile ────────────────────────────────────────────────

static esp_err_t handle_group_profile_get(httpd_req_t *req) {
    const char *current = hints_client_group_profile();
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"group_profile\":\"%s\"}",
             current ? current : "MIXED");
    return send_json(req, "200 OK", buf);
}

// ─── POST /game/group_profile ───────────────────────────────────────────────

static esp_err_t handle_group_profile_post(httpd_req_t *req) {
    if (req->content_len <= 0 ||
        req->content_len > GAME_ENDPOINT_MAX_BODY_BYTES) {
        ESP_LOGW(TAG, "POST /game/group_profile: bad body length %d",
                 (int) req->content_len);
        return send_error(req, "413 Payload Too Large",
                          "body must be 1..256 bytes");
    }

    char body[GAME_ENDPOINT_MAX_BODY_BYTES + 1] = {0};
    int  total = 0;
    while (total < (int) req->content_len) {
        int got = httpd_req_recv(req, body + total,
                                 req->content_len - total);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return send_error(req, "400 Bad Request", "recv failed");
        }
        total += got;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "POST /game/group_profile: malformed JSON: %s", body);
        return send_error(req, "400 Bad Request", "malformed json");
    }

    const cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "group_profile");
    if (!cJSON_IsString(profile) || profile->valuestring == NULL ||
        profile->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request",
                          "missing 'group_profile'");
    }

    // Push to the hints client first — its built-in whitelist is the
    // source of truth for valid profiles. If it accepts the value we
    // then persist; if it rejects we never touch NVS so a bad POST
    // cannot brick the boot-time seed.
    esp_err_t set_err = hints_client_set_group_profile(profile->valuestring);
    if (set_err != ESP_OK) {
        ESP_LOGW(TAG, "hints_client_set_group_profile(\"%s\") rejected: %s",
                 profile->valuestring, esp_err_to_name(set_err));
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", GAME_ENDPOINT_PROFILE_HELP);
    }

    // Best-effort NVS persistence. If it fails the in-RAM hints client
    // is still updated, but we surface a 500 so the operator knows the
    // change won't survive reboot. Log line above already captured the
    // underlying NVS error.
    esp_err_t nvs_err = persist_group_profile(profile->valuestring);
    if (nvs_err != ESP_OK) {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"runtime_only\","
                 "\"group_profile\":\"%s\","
                 "\"warning\":\"nvs write failed: %s\"}",
                 profile->valuestring, esp_err_to_name(nvs_err));
        cJSON_Delete(root);
        return send_json(req, "500 Internal Server Error", buf);
    }

    ESP_LOGI(TAG, "group_profile updated -> %s (NVS persisted)",
             profile->valuestring);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"group_profile\":\"%s\"}",
             profile->valuestring);
    cJSON_Delete(root);
    return send_json(req, "200 OK", buf);
}

// ─── LittleFS lazy mount (shared with media_manager — idempotent) ──────────

static bool s_storage_mounted = false;

static esp_err_t mount_storage_lazy(void) {
    if (s_storage_mounted) return ESP_OK;
    esp_vfs_littlefs_conf_t conf = {
        .base_path = GAME_ENDPOINT_STORAGE_BASE,
        .partition_label = GAME_ENDPOINT_STORAGE_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        // INVALID_STATE = already registered by another component → fine.
        s_storage_mounted = true;
        ESP_LOGI(TAG, "littlefs '%s' mounted at %s",
                 conf.partition_label, conf.base_path);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "esp_vfs_littlefs_register(%s) failed: %s",
             conf.partition_label, esp_err_to_name(err));
    return err;
}

// ─── deferred reboot (lets the HTTP response flush first) ──────────────────

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

// ─── POST /game/scenario — accept a Runtime 3 IR JSON ──────────────────────

static esp_err_t handle_scenario_post(httpd_req_t *req) {
    if (req->content_len <= 0 ||
        req->content_len > GAME_ENDPOINT_MAX_SCENARIO_BYTES) {
        ESP_LOGW(TAG, "POST /game/scenario: bad body length %d",
                 (int) req->content_len);
        return send_error(req, "413 Payload Too Large",
                          "body must be 1..65536 bytes");
    }

    if (mount_storage_lazy() != ESP_OK) {
        return send_error(req, "500 Internal Server Error",
                          "littlefs mount failed");
    }

    char *body = (char *) malloc((size_t) req->content_len + 1);
    if (!body) {
        return send_error(req, "500 Internal Server Error",
                          "out of memory");
    }
    int total = 0;
    while (total < (int) req->content_len) {
        int got = httpd_req_recv(req, body + total,
                                 req->content_len - total);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(body);
            return send_error(req, "400 Bad Request", "recv failed");
        }
        total += got;
    }
    body[total] = '\0';

    // Minimal validation: parse + schema_version + non-empty steps array.
    // The runtime3_common.py validator is the strict source of truth on
    // the gateway side; here we keep the firmware permissive but safe.
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "POST /game/scenario: malformed JSON (len=%d)", total);
        free(body);
        return send_error(req, "400 Bad Request", "malformed json");
    }
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!cJSON_IsString(schema) ||
        strcmp(schema->valuestring, "zacus.runtime3.v1") != 0) {
        cJSON_Delete(root);
        free(body);
        return send_error(req, "400 Bad Request",
                          "schema_version must be zacus.runtime3.v1");
    }
    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(root, "steps");
    if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) == 0) {
        cJSON_Delete(root);
        free(body);
        return send_error(req, "400 Bad Request",
                          "steps must be a non-empty array");
    }
    const cJSON *scenario_obj = cJSON_GetObjectItemCaseSensitive(root, "scenario");
    const cJSON *entry        = scenario_obj
        ? cJSON_GetObjectItemCaseSensitive(scenario_obj, "entry_step_id")
        : NULL;
    char entry_str[64] = {0};
    if (cJSON_IsString(entry) && entry->valuestring) {
        strncpy(entry_str, entry->valuestring, sizeof(entry_str) - 1);
    }
    int steps_count = cJSON_GetArraySize(steps);
    cJSON_Delete(root);

    // Rotate existing scenario -> .bak so a broken push can be rolled back
    // by a future scenario_engine_reload() failure path.
    struct stat st;
    if (stat(GAME_ENDPOINT_SCENARIO_PATH, &st) == 0) {
        // Best-effort: ignore rename failure (e.g. .bak already exists from
        // a previous push — overwrite via unlink+rename).
        unlink(GAME_ENDPOINT_SCENARIO_BAK);
        if (rename(GAME_ENDPOINT_SCENARIO_PATH,
                   GAME_ENDPOINT_SCENARIO_BAK) != 0) {
            ESP_LOGW(TAG, "rename current scenario -> .bak failed (errno=%d)",
                     errno);
        }
    }

    FILE *f = fopen(GAME_ENDPOINT_SCENARIO_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen %s for write failed (errno=%d)",
                 GAME_ENDPOINT_SCENARIO_PATH, errno);
        free(body);
        return send_error(req, "500 Internal Server Error",
                          "scenario write open failed");
    }
    size_t written = fwrite(body, 1, (size_t) total, f);
    fclose(f);
    free(body);

    if ((int) written != total) {
        ESP_LOGE(TAG, "scenario write short: %zu/%d bytes (rolling back)",
                 written, total);
        unlink(GAME_ENDPOINT_SCENARIO_PATH);
        if (stat(GAME_ENDPOINT_SCENARIO_BAK, &st) == 0) {
            rename(GAME_ENDPOINT_SCENARIO_BAK, GAME_ENDPOINT_SCENARIO_PATH);
        }
        return send_error(req, "500 Internal Server Error",
                          "scenario write short");
    }

    ESP_LOGI(TAG, "scenario hot-load OK: %d bytes, %d steps, entry=%s",
             total, steps_count, entry_str);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"steps_count\":%d,"
             "\"entry_step_id\":\"%s\",\"bytes\":%d,"
             "\"reload\":\"reboot_pending\"}",
             steps_count, entry_str, total);
    esp_err_t ret = send_json(req, "200 OK", buf);

    // Hot-reload-via-reboot until scenario_engine_reload() lands (Phase 3).
    // The HTTP response is queued by send_json above; the deferred task
    // gives the TCP stack 800 ms to flush before yanking the rug.
    schedule_restart();
    return ret;
}

// ─── public init ────────────────────────────────────────────────────────────

esp_err_t game_endpoint_init(httpd_handle_t server) {
    if (server == NULL) {
        ESP_LOGE(TAG, "game_endpoint_init: NULL httpd handle "
                      "(did ota_server_init() succeed?)");
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri_get = {
        .uri      = "/game/group_profile",
        .method   = HTTP_GET,
        .handler  = handle_group_profile_get,
        .user_ctx = NULL,
    };
    static const httpd_uri_t uri_post = {
        .uri      = "/game/group_profile",
        .method   = HTTP_POST,
        .handler  = handle_group_profile_post,
        .user_ctx = NULL,
    };
    static const httpd_uri_t uri_scenario_post = {
        .uri      = "/game/scenario",
        .method   = HTTP_POST,
        .handler  = handle_scenario_post,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &uri_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register GET /game/group_profile: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &uri_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register POST /game/group_profile: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &uri_scenario_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register POST /game/scenario: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "game endpoint registered "
                  "(GET+POST /game/group_profile, POST /game/scenario)");
    return ESP_OK;
}
