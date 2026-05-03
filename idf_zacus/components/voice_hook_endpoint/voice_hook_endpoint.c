// voice_hook_endpoint — see include/voice_hook_endpoint.h for the
// design notes. Slice 10 of the IDF migration: PLIP retro-telephone
// hook switch → REST → voice pipeline state machine.

#include "voice_hook_endpoint.h"

#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "voice_pipeline.h"

static const char *TAG = "voice_hook";

// ─── small JSON response helper ──────────────────────────────────────────────

static esp_err_t send_json(httpd_req_t *req, const char *status_line,
                           const char *body) {
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t send_error(httpd_req_t *req, const char *status_line,
                            const char *message) {
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message ? message : "");
    return send_json(req, status_line, buf);
}

static const char *voice_state_to_str(voice_state_t s) {
    switch (s) {
        case VOICE_STATE_IDLE:      return "idle";
        case VOICE_STATE_LISTENING: return "listening";
        case VOICE_STATE_SPEAKING:  return "speaking";
        case VOICE_STATE_MUTED:     return "muted";
        default:                    return "unknown";
    }
}

// ─── POST /voice/hook ────────────────────────────────────────────────────────

static esp_err_t handle_voice_hook_post(httpd_req_t *req) {
    if (req->content_len <= 0 ||
        req->content_len > VOICE_HOOK_MAX_BODY_BYTES) {
        ESP_LOGW(TAG, "POST /voice/hook: bad body length %d",
                 (int) req->content_len);
        return send_error(req, "413 Payload Too Large",
                          "body must be 1..256 bytes");
    }

    char body[VOICE_HOOK_MAX_BODY_BYTES + 1] = {0};
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
        ESP_LOGW(TAG, "POST /voice/hook: malformed JSON: %s", body);
        return send_error(req, "400 Bad Request", "malformed json");
    }

    const cJSON *state  = cJSON_GetObjectItemCaseSensitive(root, "state");
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");

    if (!cJSON_IsString(state) || state->valuestring == NULL) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "missing 'state'");
    }

    const char *reason_str = (cJSON_IsString(reason) && reason->valuestring)
                                 ? reason->valuestring
                                 : "(unspecified)";

    esp_err_t err = ESP_OK;

    if (strcmp(state->valuestring, VOICE_HOOK_OFF) == 0) {
        // PLIP picked up → bypass wake-word, arm capture immediately.
        ESP_LOGI(TAG, "PLIP picked up (reason=%s) — arming voice pipeline",
                 reason_str);

        // Idempotent: if we were already LISTENING the pipeline just
        // returns ESP_OK. Walk both knobs so a stale state (e.g. SPEAKING
        // from a TTS playback that ended without a clean speak_end) gets
        // forced back to LISTENING for the new conversation.
        (void) voice_pipeline_set_state(VOICE_STATE_LISTENING);
        err = voice_pipeline_start_capture();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "voice_pipeline_start_capture: %s",
                     esp_err_to_name(err));
        }

        // Open the streaming WS proactively so STT starts the moment
        // the user speaks — no need to wait for end-of-speech VAD on
        // the wake-word path.
        esp_err_t stream_err = voice_pipeline_start_streaming();
        if (stream_err != ESP_OK && stream_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "voice_pipeline_start_streaming: %s",
                     esp_err_to_name(stream_err));
        }

        cJSON_Delete(root);
        return send_json(req, "200 OK",
                         "{\"status\":\"listening\",\"mute_gate\":false}");
    }

    if (strcmp(state->valuestring, VOICE_HOOK_ON) == 0) {
        // PLIP hung up → tear streaming down, force IDLE.
        ESP_LOGI(TAG, "PLIP hung up (reason=%s) — releasing voice pipeline",
                 reason_str);

        esp_err_t stream_err = voice_pipeline_stop_streaming();
        if (stream_err != ESP_OK && stream_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "voice_pipeline_stop_streaming: %s",
                     esp_err_to_name(stream_err));
        }

        err = voice_pipeline_stop_capture();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "voice_pipeline_stop_capture: %s",
                     esp_err_to_name(err));
        }
        (void) voice_pipeline_set_state(VOICE_STATE_IDLE);

        cJSON_Delete(root);
        return send_json(req, "200 OK", "{\"status\":\"idle\"}");
    }

    ESP_LOGW(TAG, "POST /voice/hook: bad state value '%s'",
             state->valuestring);
    cJSON_Delete(root);
    return send_error(req, "400 Bad Request", "bad state");
}

// ─── GET /voice/hook/state ───────────────────────────────────────────────────

static esp_err_t handle_voice_hook_state(httpd_req_t *req) {
    voice_state_t st = voice_pipeline_get_state();
    bool wake = voice_pipeline_wake_word_active();
    bool streaming = voice_pipeline_is_streaming();

    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"voice_state\":\"%s\","
             "\"wake_word_active\":%s,"
             "\"streaming\":%s}",
             voice_state_to_str(st),
             wake ? "true" : "false",
             streaming ? "true" : "false");
    return send_json(req, "200 OK", buf);
}

// ─── 405 method-not-allowed catcher ──────────────────────────────────────────
//
// esp_http_server already returns 404 for unknown URIs. We register an
// explicit GET /voice/hook handler that returns 405 so PLIP integrators
// who confuse GET vs POST get an actionable error instead of a 404
// pointing them at the wrong fix.

static esp_err_t handle_voice_hook_get_405(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Allow", "POST");
    return send_error(req, "405 Method Not Allowed", "use POST");
}

// ─── public init ─────────────────────────────────────────────────────────────

esp_err_t voice_hook_endpoint_init(httpd_handle_t server) {
    if (server == NULL) {
        ESP_LOGE(TAG, "voice_hook_endpoint_init: NULL httpd handle "
                      "(did ota_server_init() succeed?)");
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri_post = {
        .uri      = "/voice/hook",
        .method   = HTTP_POST,
        .handler  = handle_voice_hook_post,
        .user_ctx = NULL,
    };
    static const httpd_uri_t uri_get_405 = {
        .uri      = "/voice/hook",
        .method   = HTTP_GET,
        .handler  = handle_voice_hook_get_405,
        .user_ctx = NULL,
    };
    static const httpd_uri_t uri_state = {
        .uri      = "/voice/hook/state",
        .method   = HTTP_GET,
        .handler  = handle_voice_hook_state,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &uri_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register POST /voice/hook: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &uri_get_405);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register GET /voice/hook (405): %s",
                 esp_err_to_name(err));
        return err;
    }
    err = httpd_register_uri_handler(server, &uri_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register GET /voice/hook/state: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "voice hook endpoint registered "
                  "(POST /voice/hook, GET /voice/hook/state)");
    return ESP_OK;
}
