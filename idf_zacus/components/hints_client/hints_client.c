#include "hints_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hints_client";

#define WORKER_STACK_DEFAULT  6144
#define WORKER_PRIO_DEFAULT   5
#define ASK_PATH              "/hints/ask"
#define PUZZLE_START_PATH     "/hints/puzzle_start"
#define ATTEMPT_FAILED_PATH   "/hints/attempt_failed"
#define SESSION_ID_LEN        13   // 12 hex + NUL

static struct {
    bool  ready;
    char  base_url[HINTS_CLIENT_BASE_URL_MAX];
    char  session_id[SESSION_ID_LEN];
    char  group_profile[HINTS_CLIENT_GROUP_PROFILE_MAX];
} s_client = {0};

// Whitelist of accepted group profile values. Keep in sync with the
// `group_profile` enum in the hints engine (game/hints/* server-side).
static const char *const kAllowedProfiles[] = {
    "TECH", "NON_TECH", "MIXED", "BOTH",
};
static const size_t kAllowedProfilesCount =
    sizeof(kAllowedProfiles) / sizeof(kAllowedProfiles[0]);

static bool profile_is_allowed(const char *p) {
    if (!p || !*p) return false;
    for (size_t i = 0; i < kAllowedProfilesCount; ++i) {
        if (strcmp(p, kAllowedProfiles[i]) == 0) return true;
    }
    return false;
}

// Receive buffer used by hints_client_ask. Sized to one HINTS_CLIENT_HINT_MAX
// hint plus generous JSON envelope.
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} recv_buf_t;

static void session_id_init(void) {
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(s_client.session_id, SESSION_ID_LEN,
                 "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy(s_client.session_id, "unknown-mac", SESSION_ID_LEN - 1);
    }
    s_client.session_id[SESSION_ID_LEN - 1] = '\0';
}

esp_err_t hints_client_init(const char *base_url) {
    if (!base_url || !*base_url) return ESP_ERR_INVALID_ARG;
    if (strlen(base_url) >= sizeof(s_client.base_url)) return ESP_ERR_INVALID_SIZE;

    strncpy(s_client.base_url, base_url, sizeof(s_client.base_url) - 1);
    s_client.base_url[sizeof(s_client.base_url) - 1] = '\0';
    // Default group profile until main reads NVS or the dashboard pushes
    // a new one. Validated through hints_client_set_group_profile.
    strncpy(s_client.group_profile, "MIXED",
            sizeof(s_client.group_profile) - 1);
    s_client.group_profile[sizeof(s_client.group_profile) - 1] = '\0';
    session_id_init();
    s_client.ready = true;
    ESP_LOGI(TAG, "ready, base_url=%s session_id=%s group_profile=%s",
             s_client.base_url, s_client.session_id, s_client.group_profile);
    return ESP_OK;
}

bool hints_client_is_ready(void) {
    return s_client.ready;
}

esp_err_t hints_client_set_group_profile(const char *profile) {
    if (!profile_is_allowed(profile)) {
        ESP_LOGW(TAG, "set_group_profile: invalid value \"%s\" — keeping \"%s\"",
                 profile ? profile : "(null)", s_client.group_profile);
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_client.group_profile, profile,
            sizeof(s_client.group_profile) - 1);
    s_client.group_profile[sizeof(s_client.group_profile) - 1] = '\0';
    ESP_LOGI(TAG, "group_profile set to \"%s\"", s_client.group_profile);
    return ESP_OK;
}

const char *hints_client_group_profile(void) {
    // Always non-NULL after init(); falls back to empty string before
    // init so callers don't need a separate ready check.
    return s_client.group_profile[0] ? s_client.group_profile : "";
}

static esp_err_t http_event_cb(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    recv_buf_t *r = (recv_buf_t *) evt->user_data;
    if (!r || !r->buf) return ESP_OK;
    int chunk = evt->data_len;
    if (r->len + chunk >= r->cap) {
        chunk = (int) (r->cap - r->len - 1);
    }
    if (chunk > 0) {
        memcpy(r->buf + r->len, evt->data, (size_t) chunk);
        r->len += (size_t) chunk;
        r->buf[r->len] = '\0';
    }
    return ESP_OK;
}

// ── Shared HTTP helper ───────────────────────────────────────────────────
//
// Performs `POST {base_url}{path}` with `body_str` as the request body
// and writes the response into `recv` (NUL-terminated, truncated to
// recv->cap-1). Returns:
//   ESP_OK        any 2xx (including 204 No Content)
//   ESP_FAIL      transport ok, non-2xx response
//   <esp_err_t>   transport-level error from esp_http_client_perform()
//
// The caller owns `body_str` and `recv->buf`. `timeout_ms` is per-request.
static esp_err_t post_json(const char *path, const char *body_str,
                           int timeout_ms, recv_buf_t *recv) {
    char url[HINTS_CLIENT_BASE_URL_MAX + 64];
    snprintf(url, sizeof(url), "%s%s", s_client.base_url, path);

    esp_http_client_config_t cfg = {
        .url             = url,
        .method          = HTTP_METHOD_POST,
        .timeout_ms      = timeout_ms,
        .event_handler   = recv ? http_event_cb : NULL,
        .user_data       = recv,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, (int) strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s perform failed: %s",
                 path, esp_err_to_name(err));
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "POST %s non-2xx %d body=%.*s",
                 path, status,
                 recv ? (int) recv->len : 0,
                 recv ? recv->buf : "");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t hints_client_ask(const char *puzzle_id, uint8_t level,
                           char *out_hint, size_t out_size) {
    if (!s_client.ready) return ESP_ERR_INVALID_STATE;
    if (!puzzle_id || !*puzzle_id || !out_hint || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(body, "puzzle_id", puzzle_id);
    cJSON_AddNumberToObject(body, "level", level);
    cJSON_AddStringToObject(body, "session_id", s_client.session_id);
    if (s_client.group_profile[0] != '\0') {
        cJSON_AddStringToObject(body, "group_profile", s_client.group_profile);
    }
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    char recv_storage[1024];
    recv_buf_t recv = {.buf = recv_storage, .cap = sizeof(recv_storage), .len = 0};
    recv_storage[0] = '\0';

    esp_err_t err = post_json(ASK_PATH, body_str,
                              HINTS_CLIENT_TIMEOUT_MS, &recv);
    free(body_str);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(recv.buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed: %.*s", (int) recv.len, recv.buf);
        return ESP_FAIL;
    }
    cJSON *refused = cJSON_GetObjectItemCaseSensitive(root, "refused");
    if (cJSON_IsTrue(refused)) {
        cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        const char *reason_str = (reason && cJSON_IsString(reason))
            ? reason->valuestring : "unknown";
        snprintf(out_hint, out_size,
                 "Le Professeur Zacus reste muet pour l'instant.");
        ESP_LOGI(TAG, "refused: %s", reason_str);
        cJSON_Delete(root);
        return ESP_OK;  // refusal is a valid response, not an error
    }
    cJSON *hint = cJSON_GetObjectItemCaseSensitive(root, "hint");
    if (!hint || !cJSON_IsString(hint)) {
        ESP_LOGW(TAG, "no hint field in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    strncpy(out_hint, hint->valuestring, out_size - 1);
    out_hint[out_size - 1] = '\0';
    cJSON_Delete(root);
    return ESP_OK;
}

// ── Lifecycle endpoints (slice 11 / P5) ────────────────────────────────────

// Shared body builder for /puzzle_start and /attempt_failed.
// Both endpoints take the same minimal payload {session_id, puzzle_id}.
static esp_err_t lifecycle_post(const char *path, const char *puzzle_id) {
    if (!s_client.ready) return ESP_ERR_INVALID_STATE;
    if (!puzzle_id || !*puzzle_id) return ESP_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(body, "session_id", s_client.session_id);
    cJSON_AddStringToObject(body, "puzzle_id", puzzle_id);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    char recv_storage[256];
    recv_buf_t recv = {.buf = recv_storage, .cap = sizeof(recv_storage), .len = 0};
    recv_storage[0] = '\0';

    esp_err_t err = post_json(path, body_str,
                              HINTS_CLIENT_LIFECYCLE_TIMEOUT_MS, &recv);
    free(body_str);
    return err;
}

esp_err_t hints_client_puzzle_start(const char *puzzle_id) {
    esp_err_t err = lifecycle_post(PUZZLE_START_PATH, puzzle_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "puzzle_start ok puzzle=\"%s\"", puzzle_id);
    } else {
        ESP_LOGW(TAG, "puzzle_start best-effort failed (puzzle=\"%s\"): %s",
                 puzzle_id ? puzzle_id : "(null)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t hints_client_attempt_failed(const char *puzzle_id) {
    esp_err_t err = lifecycle_post(ATTEMPT_FAILED_PATH, puzzle_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "attempt_failed ok puzzle=\"%s\"", puzzle_id);
    } else {
        ESP_LOGW(TAG, "attempt_failed best-effort failed (puzzle=\"%s\"): %s",
                 puzzle_id ? puzzle_id : "(null)", esp_err_to_name(err));
    }
    return err;
}

// ── Async wrapper ──────────────────────────────────────────────────────────

typedef struct {
    char                       puzzle_id_str[64];
    uint8_t                    puzzle_id_num;
    uint8_t                    level;
    hints_client_callback_t    cb;
    void                       *user_ctx;
} async_arg_t;

static void async_worker(void *pv) {
    async_arg_t *arg = (async_arg_t *) pv;
    char hint[HINTS_CLIENT_HINT_MAX];
    hint[0] = '\0';
    esp_err_t err = hints_client_ask(arg->puzzle_id_str, arg->level,
                                     hint, sizeof(hint));
    if (arg->cb) {
        arg->cb(arg->puzzle_id_num, arg->level, err,
                err == ESP_OK ? hint : NULL, arg->user_ctx);
    }
    free(arg);
    vTaskDelete(NULL);
}

esp_err_t hints_client_ask_async(const char *puzzle_id_str,
                                 uint8_t puzzle_id_num,
                                 uint8_t level,
                                 hints_client_callback_t cb,
                                 void *user_ctx,
                                 uint32_t stack,
                                 uint8_t prio) {
    if (!s_client.ready) return ESP_ERR_INVALID_STATE;
    if (!puzzle_id_str || !*puzzle_id_str || !cb) return ESP_ERR_INVALID_ARG;

    async_arg_t *arg = (async_arg_t *) calloc(1, sizeof(async_arg_t));
    if (!arg) return ESP_ERR_NO_MEM;
    strncpy(arg->puzzle_id_str, puzzle_id_str, sizeof(arg->puzzle_id_str) - 1);
    arg->puzzle_id_num = puzzle_id_num;
    arg->level         = level;
    arg->cb            = cb;
    arg->user_ctx      = user_ctx;

    BaseType_t ok = xTaskCreate(async_worker, "hints_async",
                                stack ? stack : WORKER_STACK_DEFAULT,
                                arg,
                                prio ? prio : WORKER_PRIO_DEFAULT,
                                NULL);
    if (ok != pdPASS) {
        free(arg);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
