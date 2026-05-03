// Internal WebSocket helper for voice_pipeline. Split out so the AFE /
// VAD state machine in voice_pipeline.c doesn't have to deal with
// esp_websocket_client wiring directly.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "voice_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure the WebSocket helper with the bridge URL and the
// session_id derived from the device MAC. Safe to call multiple times;
// the connection is opened lazily on the first
// voice_ws_open_streaming() call.
esp_err_t voice_ws_configure(const char *url,
                             const char *session_id,
                             uint32_t sample_rate_hz);

// Register the STT callback (forwarded to user code by the WS event
// handler when the bridge sends `{"type":"stt", ...}`).
void voice_ws_set_stt_callback(voice_stt_callback_t cb, void *user_ctx);

// Open the WebSocket and send the `hello` handshake. Idempotent: if
// already connected and streaming, returns ESP_OK immediately.
// Blocks up to ~5 s waiting for connect; returns ESP_ERR_TIMEOUT on
// connect failure.
esp_err_t voice_ws_open_streaming(void);

// Send a binary PCM chunk (16-bit mono little-endian). Caller owns
// the buffer. Drops silently if the WS is not currently open
// (returns ESP_ERR_INVALID_STATE).
esp_err_t voice_ws_send_chunk(const int16_t *pcm, size_t samples);

// Send the `{"type":"end"}` control frame, then close the WS. Safe
// to call from any task. After this the WS is fully torn down — a
// new open_streaming() call will reconnect from scratch.
esp_err_t voice_ws_close_streaming(void);

// True between open_streaming and close_streaming.
bool voice_ws_is_streaming(void);

// True if the helper has a configured URL (i.e. streaming is wired).
bool voice_ws_is_configured(void);

#ifdef __cplusplus
}
#endif
