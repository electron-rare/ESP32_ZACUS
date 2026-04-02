/*
 * voice_ws_client.h — WebSocket client for mascarade voice bridge
 *
 * Protocol: ws://<host>:8200/voice/ws
 * - JSON handshake (hello / hello_ack)
 * - Binary frames: PCM16 16kHz mono 20ms (320 samples, 640 bytes)
 * - JSON control messages: listen, stt, tts, error, text_query
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_STATE_IDLE,
    VOICE_STATE_CONNECTING,
    VOICE_STATE_READY,
    VOICE_STATE_LISTENING,
    VOICE_STATE_PROCESSING,
    VOICE_STATE_PLAYING,
    VOICE_STATE_ERROR
} voice_state_t;

/**
 * Initialize the WebSocket client (does not connect yet).
 * Configures the client with URL from Kconfig and registers event handlers.
 */
esp_err_t voice_ws_init(void);

/**
 * Connect to the voice bridge and perform the hello handshake.
 * Blocks until hello_ack is received or timeout (5s).
 */
esp_err_t voice_ws_connect(void);

/**
 * Disconnect from the voice bridge gracefully.
 */
esp_err_t voice_ws_disconnect(void);

/**
 * Send a PCM16 audio frame to the bridge.
 * @param pcm     Pointer to signed 16-bit PCM samples
 * @param samples Number of samples (typically AUDIO_FRAME_SAMPLES = 320)
 */
esp_err_t voice_ws_send_audio(const int16_t *pcm, size_t samples);

/**
 * Send a text query (bypass voice, direct text input).
 */
esp_err_t voice_ws_send_text_query(const char *text);

/**
 * Signal the bridge to stop listening.
 */
esp_err_t voice_ws_send_listen_stop(void);

/**
 * Abort current operation (cancel listen/processing).
 */
esp_err_t voice_ws_abort(void);

/**
 * Get current voice pipeline state.
 */
voice_state_t voice_ws_get_state(void);

/**
 * Register callback for incoming audio data (TTS playback).
 * Called from the WebSocket event task — keep it fast.
 * @param cb  Callback receiving raw PCM16 audio data and byte length.
 */
void voice_ws_set_audio_callback(void (*cb)(const uint8_t *audio_data, size_t len));

#ifdef __cplusplus
}
#endif
