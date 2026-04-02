/*
 * voice_ws_client.c — WebSocket client for mascarade voice bridge
 *
 * Connects to the voice bridge at ws://<host>:8200/voice/ws,
 * streams PCM16 audio frames, receives TTS audio and JSON control messages.
 *
 * Current version: raw PCM16 (no OPUS encoding).
 * The bridge supports WAV/PCM fallback so this works out of the box.
 *
 * TODO(opus): Add OPUS encode/decode when esp-opus component is available.
 *   - Encode: voice_ws_send_audio() should OPUS-encode before sending
 *   - Decode: ws_event_handler WEBSOCKET_EVENT_DATA binary path should
 *     OPUS-decode before forwarding to audio callback
 *   - Handshake should advertise "codec":"opus" in hello message
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

#include "voice_ws_client.h"
#include "board_config.h"

static const char *TAG = "voice-ws";

/* --------------- Internal state --------------- */

static esp_websocket_client_handle_t s_ws_client = NULL;
static SemaphoreHandle_t             s_state_mutex = NULL;
static EventGroupHandle_t            s_events = NULL;
static voice_state_t                 s_state = VOICE_STATE_IDLE;
static void (*s_audio_cb)(const uint8_t *data, size_t len) = NULL;

/* Event bits */
#define EVT_CONNECTED    BIT0
#define EVT_HELLO_ACK    BIT1
#define EVT_DISCONNECTED BIT2

/* Reconnect parameters */
#define RECONNECT_DELAY_MS  5000
#define HANDSHAKE_TIMEOUT_MS 5000

/* --------------- State helpers --------------- */

static void set_state(voice_state_t new_state)
{
    if (s_state_mutex) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
    s_state = new_state;
    if (s_state_mutex) {
        xSemaphoreGive(s_state_mutex);
    }
}

voice_state_t voice_ws_get_state(void)
{
    voice_state_t st;
    if (s_state_mutex) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        st = s_state;
        xSemaphoreGive(s_state_mutex);
    } else {
        st = s_state;
    }
    return st;
}

void voice_ws_set_audio_callback(void (*cb)(const uint8_t *audio_data, size_t len))
{
    s_audio_cb = cb;
}

/* --------------- JSON helpers --------------- */

static esp_err_t ws_send_json(const char *json_str)
{
    if (!s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) {
        ESP_LOGW(TAG, "Cannot send JSON — not connected");
        return ESP_ERR_INVALID_STATE;
    }

    int len = strlen(json_str);
    int sent = esp_websocket_client_send_text(s_ws_client, json_str, len, pdMS_TO_TICKS(2000));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send JSON (%d bytes)", len);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Sent JSON: %s", json_str);
    return ESP_OK;
}

static void send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddStringToObject(hello, "device_id", "zacus-box3");
    cJSON_AddStringToObject(hello, "codec", "pcm16");  /* TODO(opus): change to "opus" */
    cJSON_AddNumberToObject(hello, "sample_rate", AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(hello, "channels", AUDIO_CHANNELS);
    cJSON_AddNumberToObject(hello, "frame_ms", AUDIO_FRAME_MS);

    char *str = cJSON_PrintUnformatted(hello);
    if (str) {
        ESP_LOGI(TAG, "Sending hello: %s", str);
        ws_send_json(str);
        free(str);
    }
    cJSON_Delete(hello);
}

/* --------------- JSON message handler --------------- */

static void handle_json_message(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON message");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        ESP_LOGW(TAG, "JSON message missing 'type' field");
        cJSON_Delete(root);
        return;
    }

    const char *msg_type = type->valuestring;

    if (strcmp(msg_type, "hello_ack") == 0) {
        ESP_LOGI(TAG, "Received hello_ack — bridge ready");
        cJSON *caps = cJSON_GetObjectItem(root, "capabilities");
        if (caps) {
            char *caps_str = cJSON_PrintUnformatted(caps);
            if (caps_str) {
                ESP_LOGI(TAG, "Bridge capabilities: %s", caps_str);
                free(caps_str);
            }
        }
        set_state(VOICE_STATE_READY);
        xEventGroupSetBits(s_events, EVT_HELLO_ACK);

    } else if (strcmp(msg_type, "listen_ack") == 0) {
        ESP_LOGI(TAG, "Bridge acknowledged listen start");
        set_state(VOICE_STATE_LISTENING);

    } else if (strcmp(msg_type, "stt") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        cJSON *is_final = cJSON_GetObjectItem(root, "final");
        if (text && cJSON_IsString(text)) {
            ESP_LOGI(TAG, "STT %s: %s",
                     (is_final && cJSON_IsTrue(is_final)) ? "FINAL" : "partial",
                     text->valuestring);
        }
        if (is_final && cJSON_IsTrue(is_final)) {
            set_state(VOICE_STATE_PROCESSING);
        }

    } else if (strcmp(msg_type, "tts_start") == 0) {
        ESP_LOGI(TAG, "TTS playback starting");
        set_state(VOICE_STATE_PLAYING);

    } else if (strcmp(msg_type, "tts_end") == 0) {
        ESP_LOGI(TAG, "TTS playback complete");
        set_state(VOICE_STATE_READY);

    } else if (strcmp(msg_type, "error") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ESP_LOGE(TAG, "Bridge error: %s",
                 (msg && cJSON_IsString(msg)) ? msg->valuestring : "unknown");
        set_state(VOICE_STATE_ERROR);
        /* Recover to ready after a brief delay */
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (esp_websocket_client_is_connected(s_ws_client)) {
            set_state(VOICE_STATE_READY);
        }

    } else if (strcmp(msg_type, "hint") == 0) {
        cJSON *puzzle = cJSON_GetObjectItem(root, "puzzle");
        cJSON *level = cJSON_GetObjectItem(root, "level");
        cJSON *text = cJSON_GetObjectItem(root, "text");
        ESP_LOGI(TAG, "Hint [puzzle=%s level=%s]: %s",
                 (puzzle && cJSON_IsString(puzzle)) ? puzzle->valuestring : "?",
                 (level && cJSON_IsString(level)) ? level->valuestring : "?",
                 (text && cJSON_IsString(text)) ? text->valuestring : "");

    } else {
        ESP_LOGD(TAG, "Unhandled message type: %s", msg_type);
    }

    cJSON_Delete(root);
}

/* --------------- WebSocket event handler --------------- */

static void ws_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        set_state(VOICE_STATE_CONNECTING);
        xEventGroupSetBits(s_events, EVT_CONNECTED);
        xEventGroupClearBits(s_events, EVT_DISCONNECTED);
        /* Send hello handshake immediately */
        send_hello();
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        set_state(VOICE_STATE_IDLE);
        xEventGroupSetBits(s_events, EVT_DISCONNECTED);
        xEventGroupClearBits(s_events, EVT_CONNECTED | EVT_HELLO_ACK);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) {
            /* Text frame — JSON control message */
            if (data->data_ptr && data->data_len > 0) {
                handle_json_message(data->data_ptr, data->data_len);
            }
        } else if (data->op_code == 0x02) {
            /* Binary frame — TTS audio data (PCM16 or OPUS) */
            /* TODO(opus): OPUS-decode here before forwarding */
            if (s_audio_cb && data->data_ptr && data->data_len > 0) {
                s_audio_cb((const uint8_t *)data->data_ptr, data->data_len);
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        set_state(VOICE_STATE_ERROR);
        break;

    default:
        break;
    }
}

/* --------------- Reconnect task --------------- */

static void reconnect_task(void *arg)
{
    ESP_LOGI(TAG, "Reconnect monitor started");
    while (1) {
        /* Wait for disconnect event */
        EventBits_t bits = xEventGroupWaitBits(s_events, EVT_DISCONNECTED,
                                                pdFALSE, pdFALSE, portMAX_DELAY);
        if (bits & EVT_DISCONNECTED) {
            ESP_LOGI(TAG, "Auto-reconnect in %d ms...", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));

            /* Only reconnect if still disconnected and client exists */
            if (s_ws_client && !esp_websocket_client_is_connected(s_ws_client)) {
                ESP_LOGI(TAG, "Attempting reconnect...");
                esp_websocket_client_start(s_ws_client);
            }
        }
    }
}

/* --------------- Public API --------------- */

esp_err_t voice_ws_init(void)
{
    if (s_ws_client) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Create synchronization primitives */
    s_state_mutex = xSemaphoreCreateMutex();
    if (!s_state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    /* Build URL with optional token query param */
    char url[256];
    const char *base_url = CONFIG_ZACUS_VOICE_BRIDGE_URL;

#ifdef CONFIG_ZACUS_VOICE_TOKEN
    if (strlen(CONFIG_ZACUS_VOICE_TOKEN) > 0) {
        snprintf(url, sizeof(url), "%s?token=%s", base_url, CONFIG_ZACUS_VOICE_TOKEN);
    } else {
        snprintf(url, sizeof(url), "%s", base_url);
    }
#else
    snprintf(url, sizeof(url), "%s", base_url);
#endif

    ESP_LOGI(TAG, "Voice bridge URL: %s", url);

    /* Configure WebSocket client */
    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .buffer_size = 2048,
        .reconnect_timeout_ms = RECONNECT_DELAY_MS,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 15,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        return ESP_FAIL;
    }

    /* Register event handler */
    esp_err_t ret = esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                                   ws_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WS events: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start reconnect monitor task */
    xTaskCreate(reconnect_task, "ws_reconnect", 3072, NULL, 3, NULL);

    set_state(VOICE_STATE_IDLE);
    ESP_LOGI(TAG, "Voice WS client initialized");
    return ESP_OK;
}

esp_err_t voice_ws_connect(void)
{
    if (!s_ws_client) {
        ESP_LOGE(TAG, "Not initialized — call voice_ws_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    set_state(VOICE_STATE_CONNECTING);
    ESP_LOGI(TAG, "Connecting to voice bridge...");

    esp_err_t ret = esp_websocket_client_start(s_ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WS client: %s", esp_err_to_name(ret));
        set_state(VOICE_STATE_ERROR);
        return ret;
    }

    /* Wait for hello_ack */
    EventBits_t bits = xEventGroupWaitBits(s_events, EVT_HELLO_ACK,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(HANDSHAKE_TIMEOUT_MS));
    if (!(bits & EVT_HELLO_ACK)) {
        ESP_LOGW(TAG, "Handshake timeout — bridge may not have replied");
        /* Don't disconnect; the bridge might still come up */
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Voice bridge handshake complete — state=READY");
    return ESP_OK;
}

esp_err_t voice_ws_disconnect(void)
{
    if (!s_ws_client) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Disconnecting from voice bridge...");

    /* Send a goodbye message (best-effort) */
    ws_send_json("{\"type\":\"bye\"}");
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t ret = esp_websocket_client_stop(s_ws_client);
    set_state(VOICE_STATE_IDLE);
    return ret;
}

esp_err_t voice_ws_send_audio(const int16_t *pcm, size_t samples)
{
    if (!s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) {
        return ESP_ERR_INVALID_STATE;
    }

    voice_state_t st = voice_ws_get_state();
    if (st != VOICE_STATE_LISTENING && st != VOICE_STATE_READY) {
        ESP_LOGD(TAG, "Not in listening state, ignoring audio frame");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * TODO(opus): OPUS encode the PCM16 samples here.
     *   opus_encode(encoder, pcm, samples, opus_buf, opus_buf_size);
     *   Then send opus_buf as binary frame instead of raw PCM.
     *
     * For now, send raw PCM16 little-endian — the bridge has WAV/PCM fallback.
     */

    size_t byte_len = samples * sizeof(int16_t);
    int sent = esp_websocket_client_send_bin(s_ws_client, (const char *)pcm,
                                              byte_len, pdMS_TO_TICKS(500));
    if (sent < 0) {
        ESP_LOGW(TAG, "Failed to send audio frame (%d samples)", (int)samples);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t voice_ws_send_text_query(const char *text)
{
    if (!text || strlen(text) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "text_query");
    cJSON_AddStringToObject(msg, "text", text);

    char *str = cJSON_PrintUnformatted(msg);
    esp_err_t ret = ESP_FAIL;
    if (str) {
        ret = ws_send_json(str);
        free(str);
    }
    cJSON_Delete(msg);

    if (ret == ESP_OK) {
        set_state(VOICE_STATE_PROCESSING);
    }
    return ret;
}

esp_err_t voice_ws_send_listen_stop(void)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "listen");
    cJSON_AddStringToObject(msg, "state", "stop");

    char *str = cJSON_PrintUnformatted(msg);
    esp_err_t ret = ESP_FAIL;
    if (str) {
        ret = ws_send_json(str);
        free(str);
    }
    cJSON_Delete(msg);
    return ret;
}

esp_err_t voice_ws_abort(void)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "abort");

    char *str = cJSON_PrintUnformatted(msg);
    esp_err_t ret = ESP_FAIL;
    if (str) {
        ret = ws_send_json(str);
        free(str);
    }
    cJSON_Delete(msg);

    if (ret == ESP_OK) {
        set_state(VOICE_STATE_READY);
    }
    return ret;
}
