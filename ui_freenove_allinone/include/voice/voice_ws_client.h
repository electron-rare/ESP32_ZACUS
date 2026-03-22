#pragma once
#include <Arduino.h>

namespace zacus {
namespace voice {

/// WebSocket client state
enum class WsClientState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    HANDSHAKE_DONE,
    QUERY_PENDING,
    RECEIVING_AUDIO,
    ERROR
};

/// Configuration
struct VoiceWsConfig {
    String server_url = "ws://192.168.0.120:8200/voice/ws";
    String device_id = "zacus-freenove";
    String auth_token = "";
    uint32_t reconnect_delay_ms = 5000;
    uint32_t query_timeout_ms = 15000;
};

/// Initialize WS client (call from setup)
bool voiceWsInit(const VoiceWsConfig& config);

/// Connect to voice bridge (non-blocking, runs in background task)
void voiceWsConnect();

/// Disconnect
void voiceWsDisconnect();

/// Send a text query to Professor Zacus
/// Returns true if the query was sent, false if not connected
bool voiceWsTextQuery(const char* text);

/// Get current state
WsClientState voiceWsGetState();

/// Check if audio response is available
bool voiceWsHasAudio();

/// Get audio response buffer (caller must not free)
/// Returns nullptr if no audio available
const uint8_t* voiceWsGetAudio(size_t* outSize);

/// Clear audio buffer after playback
void voiceWsClearAudio();

/// Get last text response from LLM
const char* voiceWsGetLastResponse();

/// Tick function — call from loop() to process WS messages
void voiceWsTick();

} // namespace voice
} // namespace zacus
