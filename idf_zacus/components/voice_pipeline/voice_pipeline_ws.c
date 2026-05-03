// voice_pipeline_ws — WebSocket streaming layer for the Zacus voice
// pipeline. Talks to the MacStudio voice-bridge:
//
//   client → server (text, on connect):
//     {"type":"hello","version":1,"sample_rate":16000,
//      "format":"pcm_s16","session_id":"<mac>"}
//
//   client → server (binary, while streaming):
//     raw little-endian int16 PCM mono frames (typically ~32 ms each,
//     i.e. 512 samples / 1024 bytes — sized by the AFE feed chunk).
//
//   client → server (text, end of utterance):
//     {"type":"end"}
//
//   server → client (text):
//     {"type":"stt","text":"...","final":true|false}
//     {"type":"intent","content":"...","model":"..."}
//     {"type":"error","message":"..."}
//
// This slice (7) only consumes `stt` (forwarded to the user callback)
// and logs `intent` / `error`. Acting on intents and replaying TTS
// audio land in subsequent slices.

#include "voice_pipeline_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "voice_ws";

#define WS_CONNECT_TIMEOUT_MS  5000
#define WS_NETWORK_TIMEOUT_MS  5000
#define WS_HELLO_BUF_LEN       192
#define WS_RX_BUFFER_BYTES     1024
#define WS_TASK_STACK_BYTES    6144

// Event group bits for the connect handshake.
#define WS_BIT_CONNECTED  BIT0
#define WS_BIT_FAILED     BIT1

static struct {
    bool                       configured;
    char                       url[160];
    char                       session_id[32];
    uint32_t                   sample_rate_hz;

    voice_stt_callback_t       stt_cb;
    void                      *stt_cb_ctx;

    esp_websocket_client_handle_t client;
    EventGroupHandle_t            ev;
    bool                          streaming;
} s_ws = {0};

static void handle_text_message(const char *data, int len) {
    cJSON *root = cJSON_ParseWithLength(data, (size_t) len);
    if (!root) {
        ESP_LOGW(TAG, "rx: malformed JSON (%d bytes): %.*s",
                 len, len > 64 ? 64 : len, data);
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "stt") == 0) {
        const cJSON *text  = cJSON_GetObjectItemCaseSensitive(root, "text");
        const cJSON *final = cJSON_GetObjectItemCaseSensitive(root, "final");
        bool is_final = cJSON_IsBool(final) ? cJSON_IsTrue(final) : false;
        if (cJSON_IsString(text) && text->valuestring) {
            ESP_LOGI(TAG, "stt(final=%d): %s", is_final, text->valuestring);
            if (s_ws.stt_cb) {
                s_ws.stt_cb(text->valuestring, is_final, s_ws.stt_cb_ctx);
            }
        }
    } else if (strcmp(type->valuestring, "intent") == 0) {
        const cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
        const cJSON *model   = cJSON_GetObjectItemCaseSensitive(root, "model");
        ESP_LOGI(TAG, "intent (model=%s): %s",
                 cJSON_IsString(model)   ? model->valuestring   : "?",
                 cJSON_IsString(content) ? content->valuestring : "?");
        // Slice 7: log only — wiring intent into npc_engine is the next slice.
    } else if (strcmp(type->valuestring, "error") == 0) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        ESP_LOGW(TAG, "bridge error: %s",
                 cJSON_IsString(msg) ? msg->valuestring : "(no message)");
    } else {
        ESP_LOGD(TAG, "rx: unknown type=%s", type->valuestring);
    }

    cJSON_Delete(root);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data) {
    (void) handler_args;
    (void) base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *) event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ws connected to %s", s_ws.url);
        if (s_ws.ev) xEventGroupSetBits(s_ws.ev, WS_BIT_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ws disconnected");
        // Don't auto-reconnect this slice — capture task is the only
        // producer and it'll just notice via voice_ws_is_streaming().
        s_ws.streaming = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!data) break;
        // op_code 0x1 = text frame, 0x2 = binary, 0x8 = close, 0x9/0xA = ping/pong.
        // The voice-bridge only sends us text; ignore binary just in case.
        if (data->op_code == 0x01 && data->data_len > 0) {
            handle_text_message((const char *) data->data_ptr, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "ws error event");
        if (s_ws.ev) xEventGroupSetBits(s_ws.ev, WS_BIT_FAILED);
        break;
    default:
        break;
    }
}

esp_err_t voice_ws_configure(const char *url,
                             const char *session_id,
                             uint32_t sample_rate_hz) {
    if (!url || !*url) return ESP_ERR_INVALID_ARG;
    if (strlen(url) >= sizeof(s_ws.url)) return ESP_ERR_INVALID_SIZE;

    strncpy(s_ws.url, url, sizeof(s_ws.url) - 1);
    s_ws.url[sizeof(s_ws.url) - 1] = '\0';

    if (session_id && *session_id) {
        strncpy(s_ws.session_id, session_id, sizeof(s_ws.session_id) - 1);
    } else {
        strncpy(s_ws.session_id, "unknown", sizeof(s_ws.session_id) - 1);
    }
    s_ws.session_id[sizeof(s_ws.session_id) - 1] = '\0';

    s_ws.sample_rate_hz = sample_rate_hz ? sample_rate_hz : 16000;
    s_ws.configured = true;

    ESP_LOGI(TAG, "configured url=%s session=%s sr=%u",
             s_ws.url, s_ws.session_id, (unsigned) s_ws.sample_rate_hz);
    return ESP_OK;
}

void voice_ws_set_stt_callback(voice_stt_callback_t cb, void *user_ctx) {
    s_ws.stt_cb     = cb;
    s_ws.stt_cb_ctx = user_ctx;
}

bool voice_ws_is_configured(void) {
    return s_ws.configured;
}

bool voice_ws_is_streaming(void) {
    return s_ws.streaming;
}

static esp_err_t send_hello(void) {
    char buf[WS_HELLO_BUF_LEN];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"hello\",\"version\":1,"
                     "\"sample_rate\":%u,\"format\":\"pcm_s16\","
                     "\"session_id\":\"%s\"}",
                     (unsigned) s_ws.sample_rate_hz,
                     s_ws.session_id);
    if (n <= 0 || n >= (int) sizeof(buf)) return ESP_ERR_INVALID_SIZE;

    int sent = esp_websocket_client_send_text(s_ws.client, buf, n,
                                              pdMS_TO_TICKS(2000));
    if (sent < 0) {
        ESP_LOGW(TAG, "hello send failed (rc=%d)", sent);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "hello sent: %s", buf);
    return ESP_OK;
}

esp_err_t voice_ws_open_streaming(void) {
    if (!s_ws.configured) return ESP_ERR_INVALID_STATE;
    if (s_ws.streaming) return ESP_OK;

    if (!s_ws.ev) {
        s_ws.ev = xEventGroupCreate();
        if (!s_ws.ev) return ESP_ERR_NO_MEM;
    }
    xEventGroupClearBits(s_ws.ev, WS_BIT_CONNECTED | WS_BIT_FAILED);

    if (!s_ws.client) {
        const esp_websocket_client_config_t cfg = {
            .uri                = s_ws.url,
            .reconnect_timeout_ms = WS_NETWORK_TIMEOUT_MS,
            .network_timeout_ms = WS_NETWORK_TIMEOUT_MS,
            .buffer_size        = WS_RX_BUFFER_BYTES,
            .task_stack         = WS_TASK_STACK_BYTES,
            .disable_auto_reconnect = true,
        };
        s_ws.client = esp_websocket_client_init(&cfg);
        if (!s_ws.client) {
            ESP_LOGE(TAG, "esp_websocket_client_init failed");
            return ESP_FAIL;
        }
        esp_err_t reg = esp_websocket_register_events(
            s_ws.client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
        if (reg != ESP_OK) {
            ESP_LOGE(TAG, "register_events: %s", esp_err_to_name(reg));
            esp_websocket_client_destroy(s_ws.client);
            s_ws.client = NULL;
            return reg;
        }
    }

    esp_err_t err = esp_websocket_client_start(s_ws.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ws_client_start: %s", esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_ws.ev, WS_BIT_CONNECTED | WS_BIT_FAILED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WS_CONNECT_TIMEOUT_MS));

    if (!(bits & WS_BIT_CONNECTED)) {
        ESP_LOGW(TAG, "ws connect timeout (%d ms) — closing",
                 WS_CONNECT_TIMEOUT_MS);
        esp_websocket_client_stop(s_ws.client);
        return ESP_ERR_TIMEOUT;
    }

    s_ws.streaming = true;
    esp_err_t hello_err = send_hello();
    if (hello_err != ESP_OK) {
        ESP_LOGW(TAG, "hello failed (%s) — tearing down",
                 esp_err_to_name(hello_err));
        s_ws.streaming = false;
        esp_websocket_client_stop(s_ws.client);
        return hello_err;
    }
    return ESP_OK;
}

esp_err_t voice_ws_send_chunk(const int16_t *pcm, size_t samples) {
    if (!s_ws.streaming || !s_ws.client) return ESP_ERR_INVALID_STATE;
    if (!pcm || samples == 0) return ESP_ERR_INVALID_ARG;

    const size_t bytes = samples * sizeof(int16_t);
    int sent = esp_websocket_client_send_bin(
        s_ws.client, (const char *) pcm, (int) bytes, pdMS_TO_TICKS(500));
    if (sent < 0) {
        ESP_LOGW(TAG, "send_bin failed (rc=%d, %u B)", sent, (unsigned) bytes);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t voice_ws_close_streaming(void) {
    if (!s_ws.client) return ESP_OK;
    if (s_ws.streaming) {
        const char *end_msg = "{\"type\":\"end\"}";
        int sent = esp_websocket_client_send_text(
            s_ws.client, end_msg, (int) strlen(end_msg),
            pdMS_TO_TICKS(2000));
        if (sent < 0) {
            ESP_LOGW(TAG, "end send failed (rc=%d) — closing anyway", sent);
        } else {
            ESP_LOGI(TAG, "end frame sent");
        }
    }
    s_ws.streaming = false;
    // Keep the handle for a clean teardown but stop the inner task.
    esp_websocket_client_stop(s_ws.client);
    esp_websocket_client_destroy(s_ws.client);
    s_ws.client = NULL;
    return ESP_OK;
}
