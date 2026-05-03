// voice_pipeline — I2S capture + ESP-SR (AFE + WakeNet9) pipeline for the
// Zacus NPC voice loop.
//
// Slice 5 deliverable:
//   * I2S microphone bring-up (ESP-IDF 5.x i2s_std driver)
//   * placeholder VAD/wake-word task that just logs heartbeat
//   * state machine idle <-> listening <-> speaking <-> muted
//
// Slice 6 deliverable:
//   * managed dependency on `espressif/esp-sr` (>= 2.0)
//   * AFE pipeline (`AFE_TYPE_SR`, low-cost mode, 1 mic / no reference)
//     wired on top of the existing I2S capture task
//   * WakeNet9 wake-word detection. The active model is a placeholder
//     standard model shipped by Espressif (default: `wn9_hiesp` =
//     "Hi ESP"). Custom "Professeur Zacus" model is out of scope for
//     this slice (requires a 2-4 week training round-trip with
//     Espressif — tracked under the P2 voice-pipeline spec).
//   * wake event routes through a user callback and auto-transitions
//     the pipeline into VOICE_STATE_LISTENING.
//
// Slice 7 deliverable (this revision):
//   * managed dependency on `espressif/esp_websocket_client` (~1.4)
//   * after a wake event, post-AFE PCM chunks (16 kHz mono int16) are
//     streamed as binary WebSocket frames to the MacStudio voice-bridge
//     (`ws://studio:8200/voice/ws`).
//   * the AFE VAD output drives end-of-speech detection: ~1.5 s of
//     sustained `AFE_VAD_SILENCE` closes the upload with a JSON `end`
//     control message and returns the state machine to IDLE.
//   * STT transcripts received from the bridge fan out through a new
//     `voice_pipeline_set_stt_callback`. The hint/intent dispatch
//     itself stays in npc_engine (out of scope for this slice).
//
// The HTTP plumbing to the hints engine still lives in the separate
// hints_client component to avoid a circular dependency with npc_engine.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_LISTENING,
    VOICE_STATE_SPEAKING,
    VOICE_STATE_MUTED,
} voice_state_t;

// Callback invoked from the capture task whenever the wake-word engine
// fires. The pipeline auto-transitions to VOICE_STATE_LISTENING *before*
// invoking the callback, so the callback can safely kick off STT capture
// or play an acknowledgement cue. The string `wake_word` is owned by
// esp-sr (lifetime tied to the AFE handle); if the callback needs to
// keep it, it must copy.
typedef void (*voice_wake_callback_t)(const char *wake_word, void *user_ctx);

// Callback invoked when the voice-bridge returns a transcription. The
// callback runs on the WebSocket task context (esp_websocket_client's
// event loop) — keep it short and offload heavy work to another task.
// `text` is a NUL-terminated UTF-8 string owned by the pipeline; copy
// before returning if you need to keep it. `final == true` means the
// bridge has flagged the segment as final; `final == false` is an
// interim transcript (may still be revised).
typedef void (*voice_stt_callback_t)(const char *text, bool final,
                                     void *user_ctx);

typedef struct {
    int      i2s_bclk_pin;       // BCLK (SCK)
    int      i2s_ws_pin;         // WS (LRCK)
    int      i2s_din_pin;        // DIN  (mic data)
    uint32_t sample_rate_hz;     // 16000 default
    bool     auto_start_capture; // launch capture task at init
    bool     enable_wake_word;   // bring up esp-sr AFE + WakeNet (slice 6)

    // Slice 7: voice-bridge WebSocket endpoint, e.g.
    //   "ws://100.116.92.12:8200/voice/ws"
    // Pass NULL (default) to disable streaming entirely — the wake
    // callback still fires but no audio leaves the device. Lifetime of
    // the string must outlive `voice_pipeline_init` (typically a static
    // `#define` in main.c).
    const char *voice_bridge_ws_url;
} voice_pipeline_config_t;

// Reasonable defaults for a Freenove ESP32-S3 + INMP441 wiring. Override per
// hardware revision before calling voice_pipeline_init().
//
// Slice 6: `enable_wake_word` defaults to `false` to preserve slice-5
// behaviour for callers that only want the I2S capture stub. The Zacus
// master sets it to `true` in its own init flow.
//
// Slice 7: `voice_bridge_ws_url` defaults to NULL (streaming disabled).
void voice_pipeline_default_config(voice_pipeline_config_t *out);

esp_err_t voice_pipeline_init(const voice_pipeline_config_t *config);

esp_err_t voice_pipeline_start_capture(void);
esp_err_t voice_pipeline_stop_capture(void);

voice_state_t voice_pipeline_get_state(void);
esp_err_t     voice_pipeline_set_state(voice_state_t state);

// Register the wake-word callback. Pass `cb = NULL` to clear. May be
// called before or after init. The callback runs in the capture task
// context — keep it short and offload heavy work to another task.
esp_err_t voice_pipeline_set_wake_callback(voice_wake_callback_t cb,
                                           void *user_ctx);

// Register the STT result callback (slice 7). Pass `cb = NULL` to
// clear. May be called before or after init.
esp_err_t voice_pipeline_set_stt_callback(voice_stt_callback_t cb,
                                          void *user_ctx);

// Returns true once esp-sr AFE + WakeNet have been brought up and the
// pipeline is actively running them in the capture task. Returns false
// if init was called with `enable_wake_word = false`, if the model
// partition was missing, or if AFE/WakeNet alloc failed (in which case
// the pipeline silently degrades to the slice-5 stub capture).
bool voice_pipeline_wake_word_active(void);

// Slice 7 manual control — open / close the WebSocket stream to the
// voice-bridge without going through the wake detector. Useful for
// REST-driven smoke tests and for forcing capture during dev. Returns
// ESP_ERR_INVALID_STATE if no `voice_bridge_ws_url` was configured.
esp_err_t voice_pipeline_start_streaming(void);
esp_err_t voice_pipeline_stop_streaming(void);

// True between voice_pipeline_start_streaming and the moment the
// pipeline detected end-of-speech (or stop_streaming was called).
bool voice_pipeline_is_streaming(void);

#ifdef __cplusplus
}
#endif
