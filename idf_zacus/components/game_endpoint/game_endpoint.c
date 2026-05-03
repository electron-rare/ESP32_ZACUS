// game_endpoint — see include/game_endpoint.h for design notes.
//
// Slice 12 of the IDF migration: live-tunable group profile so the
// game master can switch the hints engine policy from the dashboard
// (or any HTTP client) without reflashing NVS.

#include "game_endpoint.h"

#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
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

    ESP_LOGI(TAG, "game endpoint registered "
                  "(GET + POST /game/group_profile)");
    return ESP_OK;
}
