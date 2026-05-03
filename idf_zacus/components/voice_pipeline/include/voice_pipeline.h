// voice_pipeline — I2S capture stub + state machine for the Zacus NPC voice
// loop. Slice 5 deliverable:
//   * I2S microphone bring-up (ESP-IDF 5.x i2s_std driver)
//   * placeholder VAD/wake-word task that just logs heartbeat
//   * state machine idle <-> listening <-> speaking <-> muted
//
// Real wake-word (esp-sr WakeNet9) and AFE pipeline land in slice 6.
//
// HTTP plumbing to the hints engine lives in the separate hints_client
// component to avoid a circular dependency with npc_engine.

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

typedef struct {
    int      i2s_bclk_pin;       // BCLK (SCK)
    int      i2s_ws_pin;         // WS (LRCK)
    int      i2s_din_pin;        // DIN  (mic data)
    uint32_t sample_rate_hz;     // 16000 default
    bool     auto_start_capture; // launch capture task at init
} voice_pipeline_config_t;

// Reasonable defaults for a Freenove ESP32-S3 + INMP441 wiring. Override per
// hardware revision before calling voice_pipeline_init().
void voice_pipeline_default_config(voice_pipeline_config_t *out);

esp_err_t voice_pipeline_init(const voice_pipeline_config_t *config);

esp_err_t voice_pipeline_start_capture(void);
esp_err_t voice_pipeline_stop_capture(void);

voice_state_t voice_pipeline_get_state(void);
esp_err_t     voice_pipeline_set_state(voice_state_t state);

#ifdef __cplusplus
}
#endif
