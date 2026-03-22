// =============================================================================
// WebSocket Client for Professeur Zacus Voice Bridge
// =============================================================================
// Connects to the voice bridge server over WebSocket and enables text-based
// queries to the LLM. Audio streaming (ESP-SR mic input) will be added later.
//
// Protocol (JSON over WebSocket text frames):
//   Client -> Server:
//     {"type":"hello","device_id":"...","capabilities":["text_query"]}
//     {"type":"text_query","text":"...","request_id":"..."}
//   Server -> Client:
//     {"type":"hello_ack","session_id":"..."}
//     {"type":"llm_response","text":"...","request_id":"..."}
//     {"type":"tts","state":"start"}
//     Binary frames: raw PCM/OPUS audio data
//     {"type":"tts","state":"stop"}
//     {"type":"error","message":"..."}
//
// Dependencies: links2004/WebSockets, bblanchon/ArduinoJson
// =============================================================================

#include "voice/voice_ws_client.h"

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static const char* TAG = "VOICE_WS";

namespace zacus {
namespace voice {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static WebSocketsClient s_ws;
static VoiceWsConfig    s_config;

static volatile WsClientState s_state = WsClientState::DISCONNECTED;
static volatile bool s_initialized    = false;
static volatile bool s_should_connect = false;

// Audio response buffer in PSRAM (up to 512KB)
static const size_t AUDIO_BUF_MAX = 512 * 1024;
static uint8_t*     s_audio_buf   = nullptr;
static volatile size_t s_audio_len = 0;
static volatile bool   s_audio_ready = false;
static volatile bool   s_receiving_audio = false;

// Last text response from LLM
static char s_last_response[2048] = {0};

// Reconnect timing
static uint32_t s_last_reconnect_attempt = 0;

// Query timeout tracking
static uint32_t s_query_sent_at = 0;

// Request ID counter
static uint32_t s_request_id_counter = 0;

// Session ID from server
static char s_session_id[64] = {0};

// Parsed host/port/path from URL
static String s_ws_host;
static uint16_t s_ws_port = 8200;
static String s_ws_path;

// ---------------------------------------------------------------------------
// URL parsing helper
// ---------------------------------------------------------------------------

/// Parse "ws://host:port/path" into components
static bool parseWsUrl(const String& url, String& host, uint16_t& port, String& path) {
    String u = url;

    // Strip ws:// or wss:// prefix
    if (u.startsWith("ws://")) {
        u = u.substring(5);
    } else if (u.startsWith("wss://")) {
        u = u.substring(6);
        Serial.printf("[%s] WARNING: wss:// not supported on ESP32 WebSocketsClient in this config\n", TAG);
    } else {
        Serial.printf("[%s] ERROR: URL must start with ws:// or wss://\n", TAG);
        return false;
    }

    // Split host:port and path
    int pathIdx = u.indexOf('/');
    String hostPort;
    if (pathIdx >= 0) {
        hostPort = u.substring(0, pathIdx);
        path = u.substring(pathIdx);
    } else {
        hostPort = u;
        path = "/";
    }

    // Split host and port
    int colonIdx = hostPort.indexOf(':');
    if (colonIdx >= 0) {
        host = hostPort.substring(0, colonIdx);
        port = (uint16_t)hostPort.substring(colonIdx + 1).toInt();
    } else {
        host = hostPort;
        port = 80;
    }

    return host.length() > 0;
}

// ---------------------------------------------------------------------------
// JSON message builders
// ---------------------------------------------------------------------------

/// Build hello handshake message
static String buildHelloMessage() {
    StaticJsonDocument<256> doc;
    doc["type"] = "hello";
    doc["device_id"] = s_config.device_id;
    doc["version"] = 1;
    JsonArray caps = doc.createNestedArray("capabilities");
    caps.add("text_query");
    // Audio streaming capability will be added when ESP-SR is integrated
    // caps.add("audio_stream");

    if (s_config.auth_token.length() > 0) {
        doc["auth_token"] = s_config.auth_token;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

/// Build text query message
static String buildTextQueryMessage(const char* text, uint32_t requestId) {
    StaticJsonDocument<512> doc;
    doc["type"] = "text_query";
    doc["text"] = text;
    doc["request_id"] = String(requestId);

    String out;
    serializeJson(doc, out);
    return out;
}

// ---------------------------------------------------------------------------
// JSON message handler
// ---------------------------------------------------------------------------

static void handleJsonMessage(const char* payload, size_t length) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.printf("[%s] JSON parse error: %s\n", TAG, err.c_str());
        return;
    }

    const char* type = doc["type"] | "";

    // --- hello_ack: server acknowledged our handshake ---
    if (strcmp(type, "hello_ack") == 0) {
        const char* sid = doc["session_id"] | "";
        strncpy(s_session_id, sid, sizeof(s_session_id) - 1);
        s_session_id[sizeof(s_session_id) - 1] = '\0';
        s_state = WsClientState::HANDSHAKE_DONE;
        Serial.printf("[%s] Handshake done, session_id=%s\n", TAG, s_session_id);
        return;
    }

    // --- llm_response: text response from LLM ---
    if (strcmp(type, "llm_response") == 0) {
        const char* text = doc["text"] | "";
        strncpy(s_last_response, text, sizeof(s_last_response) - 1);
        s_last_response[sizeof(s_last_response) - 1] = '\0';
        Serial.printf("[%s] LLM response (%d chars): %.80s%s\n", TAG,
                      (int)strlen(text), text, strlen(text) > 80 ? "..." : "");

        // If we're not receiving audio, the query is complete
        if (!s_receiving_audio) {
            s_state = WsClientState::HANDSHAKE_DONE;
            s_query_sent_at = 0;
        }
        return;
    }

    // --- tts: audio stream start/stop ---
    if (strcmp(type, "tts") == 0) {
        const char* ttsState = doc["state"] | "";

        if (strcmp(ttsState, "start") == 0) {
            Serial.printf("[%s] TTS audio stream starting\n", TAG);
            s_receiving_audio = true;
            s_audio_len = 0;
            s_audio_ready = false;
            s_state = WsClientState::RECEIVING_AUDIO;
        } else if (strcmp(ttsState, "stop") == 0) {
            Serial.printf("[%s] TTS audio stream complete, %u bytes received\n", TAG, (unsigned)s_audio_len);
            s_receiving_audio = false;
            s_audio_ready = (s_audio_len > 0);
            s_state = WsClientState::HANDSHAKE_DONE;
            s_query_sent_at = 0;
        }
        return;
    }

    // --- error: server-side error ---
    if (strcmp(type, "error") == 0) {
        const char* msg = doc["message"] | "unknown error";
        Serial.printf("[%s] Server error: %s\n", TAG, msg);
        s_state = WsClientState::ERROR;
        s_query_sent_at = 0;
        return;
    }

    // --- unknown message type ---
    Serial.printf("[%s] Unknown message type: %s\n", TAG, type);
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------

static void wsEventHandler(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

    case WStype_DISCONNECTED:
        Serial.printf("[%s] Disconnected\n", TAG);
        s_state = WsClientState::DISCONNECTED;
        s_receiving_audio = false;
        s_query_sent_at = 0;
        s_session_id[0] = '\0';
        break;

    case WStype_CONNECTED:
        Serial.printf("[%s] Connected to %s\n", TAG, s_ws_host.c_str());
        s_state = WsClientState::CONNECTED;

        // Send hello handshake
        {
            String hello = buildHelloMessage();
            s_ws.sendTXT(hello);
            Serial.printf("[%s] Sent hello handshake\n", TAG);
        }
        break;

    case WStype_TEXT:
        handleJsonMessage((const char*)payload, length);
        break;

    case WStype_BIN:
        // Binary frame = audio data from TTS
        if (s_receiving_audio && s_audio_buf) {
            size_t space = AUDIO_BUF_MAX - s_audio_len;
            size_t toCopy = (length <= space) ? length : space;
            if (toCopy > 0) {
                memcpy(s_audio_buf + s_audio_len, payload, toCopy);
                s_audio_len += toCopy;
            }
            if (toCopy < length) {
                Serial.printf("[%s] WARNING: Audio buffer full, dropped %u bytes\n",
                              TAG, (unsigned)(length - toCopy));
            }
        } else {
            Serial.printf("[%s] Received %u binary bytes outside TTS stream (ignored)\n",
                          TAG, (unsigned)length);
        }
        break;

    case WStype_PING:
        // Library handles pong automatically
        break;

    case WStype_PONG:
        break;

    case WStype_ERROR:
        Serial.printf("[%s] WebSocket error\n", TAG);
        s_state = WsClientState::ERROR;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool voiceWsInit(const VoiceWsConfig& config) {
    if (s_initialized) {
        Serial.printf("[%s] Already initialized, call voiceWsDisconnect() first\n", TAG);
        return false;
    }

    s_config = config;

    // Parse URL
    if (!parseWsUrl(config.server_url, s_ws_host, s_ws_port, s_ws_path)) {
        Serial.printf("[%s] Failed to parse server URL: %s\n", TAG, config.server_url.c_str());
        return false;
    }

    Serial.printf("[%s] Init: host=%s port=%u path=%s\n", TAG,
                  s_ws_host.c_str(), s_ws_port, s_ws_path.c_str());
    Serial.printf("[%s] Init: device_id=%s reconnect=%ums timeout=%ums\n", TAG,
                  config.device_id.c_str(), config.reconnect_delay_ms, config.query_timeout_ms);

    // Allocate audio buffer in PSRAM
    s_audio_buf = (uint8_t*)heap_caps_malloc(AUDIO_BUF_MAX, MALLOC_CAP_SPIRAM);
    if (!s_audio_buf) {
        // Fallback to regular heap (smaller buffer)
        Serial.printf("[%s] WARNING: PSRAM alloc failed, trying regular heap (64KB)\n", TAG);
        s_audio_buf = (uint8_t*)malloc(64 * 1024);
        if (!s_audio_buf) {
            Serial.printf("[%s] ERROR: Cannot allocate audio buffer\n", TAG);
            return false;
        }
    } else {
        Serial.printf("[%s] Audio buffer: %uKB in PSRAM\n", TAG, (unsigned)(AUDIO_BUF_MAX / 1024));
    }

    s_audio_len = 0;
    s_audio_ready = false;
    s_receiving_audio = false;
    s_last_response[0] = '\0';
    s_session_id[0] = '\0';
    s_state = WsClientState::DISCONNECTED;
    s_initialized = true;

    return true;
}

void voiceWsConnect() {
    if (!s_initialized) {
        Serial.printf("[%s] Not initialized, call voiceWsInit() first\n", TAG);
        return;
    }

    if (s_state != WsClientState::DISCONNECTED && s_state != WsClientState::ERROR) {
        Serial.printf("[%s] Already connected or connecting\n", TAG);
        return;
    }

    Serial.printf("[%s] Connecting to %s:%u%s\n", TAG,
                  s_ws_host.c_str(), s_ws_port, s_ws_path.c_str());

    s_state = WsClientState::CONNECTING;

    // Configure WebSocket client
    s_ws.begin(s_ws_host.c_str(), s_ws_port, s_ws_path.c_str());
    s_ws.onEvent(wsEventHandler);

    // Auto-reconnect with configured delay
    s_ws.setReconnectInterval(s_config.reconnect_delay_ms);

    // Set Origin header for CORS
    s_ws.setExtraHeaders("Origin: esp32-zacus");
}

void voiceWsDisconnect() {
    if (!s_initialized) return;

    Serial.printf("[%s] Disconnecting\n", TAG);
    s_ws.disconnect();
    s_state = WsClientState::DISCONNECTED;
    s_receiving_audio = false;
    s_query_sent_at = 0;
    s_session_id[0] = '\0';
}

bool voiceWsTextQuery(const char* text) {
    if (!text || text[0] == '\0') {
        Serial.printf("[%s] Empty query, ignoring\n", TAG);
        return false;
    }

    if (s_state != WsClientState::HANDSHAKE_DONE) {
        Serial.printf("[%s] Cannot send query in state %d (need HANDSHAKE_DONE)\n",
                      TAG, (int)s_state);
        return false;
    }

    uint32_t reqId = ++s_request_id_counter;
    String msg = buildTextQueryMessage(text, reqId);

    bool sent = s_ws.sendTXT(msg);
    if (sent) {
        s_state = WsClientState::QUERY_PENDING;
        s_query_sent_at = millis();
        // Clear previous audio buffer for new response
        s_audio_len = 0;
        s_audio_ready = false;
        Serial.printf("[%s] Query sent (req_id=%u): %.60s%s\n", TAG, reqId,
                      text, strlen(text) > 60 ? "..." : "");
    } else {
        Serial.printf("[%s] Failed to send query\n", TAG);
    }

    return sent;
}

WsClientState voiceWsGetState() {
    return s_state;
}

bool voiceWsHasAudio() {
    return s_audio_ready && s_audio_len > 0;
}

const uint8_t* voiceWsGetAudio(size_t* outSize) {
    if (!s_audio_ready || s_audio_len == 0) {
        if (outSize) *outSize = 0;
        return nullptr;
    }
    if (outSize) *outSize = s_audio_len;
    return s_audio_buf;
}

void voiceWsClearAudio() {
    s_audio_len = 0;
    s_audio_ready = false;
}

const char* voiceWsGetLastResponse() {
    if (s_last_response[0] == '\0') return nullptr;
    return s_last_response;
}

void voiceWsTick() {
    if (!s_initialized) return;

    // Process WebSocket messages (must be called from loop)
    s_ws.loop();

    // Check for query timeout
    if (s_query_sent_at > 0 &&
        (s_state == WsClientState::QUERY_PENDING || s_state == WsClientState::RECEIVING_AUDIO))
    {
        uint32_t elapsed = millis() - s_query_sent_at;
        if (elapsed > s_config.query_timeout_ms) {
            Serial.printf("[%s] Query timeout after %ums\n", TAG, elapsed);
            s_state = WsClientState::HANDSHAKE_DONE;
            s_query_sent_at = 0;
            s_receiving_audio = false;
            // Keep any partial audio/response that was received
            if (s_audio_len > 0) {
                s_audio_ready = true;
            }
        }
    }

    // Auto-reconnect is handled by WebSocketsClient internally,
    // but we track state for external consumers
    if (s_state == WsClientState::DISCONNECTED && s_should_connect) {
        uint32_t now = millis();
        if (now - s_last_reconnect_attempt >= s_config.reconnect_delay_ms) {
            s_last_reconnect_attempt = now;
            // WebSocketsClient handles reconnect internally
        }
    }
}

} // namespace voice
} // namespace zacus

// =============================================================================
// Integration guide — add to main.cpp
// =============================================================================
//
// 1. Include the header:
//    #include "voice/voice_ws_client.h"
//
// 2. In setup(), after WiFi is connected:
//    zacus::voice::VoiceWsConfig vwsCfg;
//    vwsCfg.server_url = "ws://192.168.0.120:8200/voice/ws";
//    vwsCfg.device_id  = "zacus-freenove";
//    if (zacus::voice::voiceWsInit(vwsCfg)) {
//        zacus::voice::voiceWsConnect();
//    }
//
// 3. In loop():
//    zacus::voice::voiceWsTick();
//
// 4. Web API endpoints (add to main.cpp web API section):
//
// webOnApi("/api/voice/query", HTTP_POST, []() {
//     WEB_AUTH_CHECK();
//     String text = g_web_server.arg("text");
//     if (text.isEmpty()) {
//         g_web_server.send(400, "application/json", "{\"error\":\"missing text\"}");
//         return;
//     }
//     bool ok = zacus::voice::voiceWsTextQuery(text.c_str());
//     g_web_server.send(ok ? 200 : 503, "application/json",
//         ok ? "{\"status\":\"sent\"}" : "{\"error\":\"not_connected\"}");
// });
//
// webOnApi("/api/voice/status", HTTP_GET, []() {
//     auto state = zacus::voice::voiceWsGetState();
//     const char* lastResp = zacus::voice::voiceWsGetLastResponse();
//     String json = "{\"state\":\"" + String((int)state) + "\"";
//     if (lastResp) json += ",\"last_response\":\"" + String(lastResp) + "\"";
//     json += "}";
//     g_web_server.send(200, "application/json", json);
// });
