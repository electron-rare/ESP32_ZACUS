// =============================================================================
// SCAFFOLD: Voice Pipeline for ESP-SR Integration
// =============================================================================
// This file provides the structure for voice integration with Professeur Zacus.
// All functions are stubs that log a message and return safe defaults.
//
// Full implementation requires:
//   1. ESP-IDF framework (not Arduino) with esp-sr component
//      - platformio.ini: framework = espidf  (instead of arduino)
//      - Or use arduino-as-component approach for gradual migration
//   2. esp-sr component added to idf_component.yml:
//        dependencies:
//          espressif/esp-sr: "^1.0.0"
//   3. Custom wake word model from Espressif ("Professeur Zacus")
//      - Contact: https://www.espressif.com/en/contact-us
//      - Provide 200+ audio samples of target wake phrase
//      - Turnaround: ~2-4 weeks, cost: varies by agreement
//   4. INMP441 MEMS microphone connected to I2S0 bus
//   5. mascarade bridge WebSocket endpoint for STT/LLM/TTS
//
// Architecture (XiaoZhi-inspired):
//   ┌─────────┐    ┌─────────────────────────┐    ┌──────────────┐
//   │ INMP441 │───>│ AFE (AEC + NS + VAD)    │───>│ WakeNet9     │
//   │ I2S0    │    │ esp_afe_sr_create()      │    │ "Prof Zacus" │
//   │ @16kHz  │    └─────────────────────────┘    └──────┬───────┘
//   └─────────┘                                          │ wake!
//                                                        v
//   ┌─────────┐    ┌─────────────────────────┐    ┌──────────────┐
//   │ Speaker │<───│ OPUS decode + I2S1 out  │<───│ WebSocket    │
//   │ MAX98357│    │ @24kHz 16-bit           │    │ mascarade    │
//   │ I2S1    │    └─────────────────────────┘    │ bridge:4001  │
//   └─────────┘                                   └──────────────┘
//
// Memory budget on ESP32-S3 (8MB PSRAM):
//   WakeNet9:      ~340KB PSRAM
//   AFE (1ch):     ~1.1MB PSRAM
//   OPUS codec:    ~50KB internal RAM
//   Audio buffers: ~64KB PSRAM
//   ─────────────────────────────
//   Total voice:   ~1.5MB of 8MB PSRAM
//   Remaining:     ~6.5MB for LVGL, camera, etc.
//
// WebSocket protocol (XiaoZhi-compatible):
//   Client -> Server:
//     - JSON: {"type":"hello","version":1,"wakeword":"professeur zacus"}
//     - Binary: OPUS-encoded audio frames (20ms, 16kHz, mono)
//     - JSON: {"type":"abort"} to cancel
//   Server -> Client:
//     - JSON: {"type":"stt","text":"..."} interim/final transcription
//     - JSON: {"type":"llm","text":"..."} LLM response text
//     - Binary: OPUS-encoded TTS audio
//     - JSON: {"type":"tts","state":"start|end"}
//     - JSON: {"type":"emotion","value":"happy|thinking|..."}
// =============================================================================

#include "voice/voice_pipeline.h"

// When ESP-SR is available, these headers will be needed:
// #include "esp_afe_sr_iface.h"
// #include "esp_afe_sr_models.h"
// #include "esp_mn_iface.h"
// #include "esp_mn_models.h"
// #include "esp_wn_iface.h"
// #include "esp_wn_models.h"
// #include "driver/i2s.h"
// #include "opus.h"

static const char* TAG = "VOICE";

namespace zacus {
namespace voice {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static VoicePipelineState s_state = VoicePipelineState::IDLE;
static VoiceEventCallback s_callback = nullptr;
static VoicePipelineConfig s_config;

// Placeholder handles for ESP-SR components (will be real pointers)
// static esp_afe_sr_iface_t* s_afe_handle = nullptr;
// static esp_afe_sr_data_t* s_afe_data = nullptr;
// static TaskHandle_t s_audio_task = nullptr;
// static TaskHandle_t s_afe_task = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers (stubs)
// ---------------------------------------------------------------------------

/// Notify registered callback of state change
static void notifyStateChange(VoicePipelineState new_state, const char* text = nullptr) {
    s_state = new_state;
    if (s_callback) {
        s_callback(new_state, text);
    }
    Serial.printf("[%s] State -> %s%s%s\n", TAG,
                  voicePipelineStateName(new_state),
                  text ? ": " : "",
                  text ? text : "");
}

// ---------------------------------------------------------------------------
// FreeRTOS task functions (stubs)
// ---------------------------------------------------------------------------

// When implemented, this task will:
// 1. Read 16-bit PCM samples from I2S0 (INMP441 mic) in a loop
// 2. Feed samples to AFE via esp_afe_sr_data_t->feed()
// 3. Run at highest priority to avoid audio underruns
// 4. Use a ring buffer for backpressure management
//
// static void audioFeedTask(void* param) {
//     const int buf_size = 512;  // samples per read
//     int16_t* buf = (int16_t*)heap_caps_malloc(buf_size * sizeof(int16_t),
//                                                MALLOC_CAP_SPIRAM);
//     while (true) {
//         size_t bytes_read = 0;
//         i2s_read(I2S_NUM_0, buf, buf_size * sizeof(int16_t),
//                  &bytes_read, portMAX_DELAY);
//         if (bytes_read > 0 && s_afe_data) {
//             s_afe_data->feed(s_afe_data, buf);
//         }
//     }
// }

// When implemented, this task will:
// 1. Call AFE fetch() in a loop to get processed audio
// 2. Check for wake word detection (WAKENET_DETECTED event)
// 3. On wake detection: transition to STREAMING state
// 4. In STREAMING state: OPUS-encode audio and send via WebSocket
// 5. Handle VAD silence detection for end-of-utterance
//
// static void afeDetectTask(void* param) {
//     while (true) {
//         afe_fetch_result_t* result = s_afe_data->fetch(s_afe_data);
//         if (!result) continue;
//
//         if (result->wakeup_state == WAKENET_DETECTED) {
//             notifyStateChange(VoicePipelineState::WAKE_DETECTED,
//                               result->wake_word_id);
//             // Open WebSocket, start OPUS encoder, transition to STREAMING
//         }
//
//         if (s_state == VoicePipelineState::STREAMING) {
//             // OPUS encode result->data and send via WebSocket
//         }
//     }
// }

// ---------------------------------------------------------------------------
// Public API implementation (stubs)
// ---------------------------------------------------------------------------

bool voicePipelineInit(const VoicePipelineConfig& config) {
    Serial.printf("[%s] scaffold: voicePipelineInit() — not yet implemented\n", TAG);
    Serial.printf("[%s]   mic pins: BCLK=%d, WS=%d, DATA=%d\n", TAG,
                  config.mic_bclk_pin, config.mic_ws_pin, config.mic_data_pin);
    Serial.printf("[%s]   wake word model: %s\n", TAG, config.wake_word_model);
    Serial.printf("[%s]   server: %s\n", TAG, config.server_ws_url);
    Serial.printf("[%s]   audio: %dHz, %d-bit, %dch\n", TAG,
                  config.sample_rate, config.bits_per_sample, config.channels);

    s_config = config;
    s_state = VoicePipelineState::IDLE;

    // When implemented, this will:
    // 1. i2s_driver_install(I2S_NUM_0, &i2s_config, ...)
    // 2. i2s_set_pin(I2S_NUM_0, &pin_config)
    // 3. afe_config_t cfg = AFE_CONFIG_DEFAULT();
    //    cfg.wakenet_model_name = config.wake_word_model;
    //    cfg.aec_init = true;  // echo cancellation (requires reference signal)
    //    s_afe_data = s_afe_handle->create_from_config(&cfg);
    // 4. xTaskCreatePinnedToCore(audioFeedTask, "audio_feed", 4096, NULL, 5, &s_audio_task, 0);
    // 5. xTaskCreatePinnedToCore(afeDetectTask, "afe_detect", 8192, NULL, 4, &s_afe_task, 1);

    Serial.printf("[%s] scaffold: returning false (ESP-SR not linked)\n", TAG);
    Serial.printf("[%s] To implement: switch to ESP-IDF framework + add esp-sr component\n", TAG);
    return false;
}

void voicePipelineStart() {
    Serial.printf("[%s] scaffold: voicePipelineStart() — not yet implemented\n", TAG);

    // When implemented:
    // - Resume AFE processing
    // - Transition IDLE -> LISTENING
    // - notifyStateChange(VoicePipelineState::LISTENING);
}

void voicePipelineStop() {
    Serial.printf("[%s] scaffold: voicePipelineStop() — not yet implemented\n", TAG);

    // When implemented:
    // - Suspend AFE processing
    // - Close any open WebSocket connection
    // - Stop OPUS encoder/decoder
    // - Transition -> IDLE
    // - notifyStateChange(VoicePipelineState::IDLE);
}

VoicePipelineState voicePipelineGetState() {
    return s_state;
}

void voicePipelineOnEvent(VoiceEventCallback callback) {
    s_callback = callback;
    Serial.printf("[%s] Event callback registered\n", TAG);
}

// ---------------------------------------------------------------------------
// Utility / Debug
// ---------------------------------------------------------------------------

const char* voicePipelineStateName(VoicePipelineState state) {
    switch (state) {
        case VoicePipelineState::IDLE:          return "IDLE";
        case VoicePipelineState::LISTENING:     return "LISTENING";
        case VoicePipelineState::WAKE_DETECTED: return "WAKE_DETECTED";
        case VoicePipelineState::COMMAND:       return "COMMAND";
        case VoicePipelineState::STREAMING:     return "STREAMING";
        case VoicePipelineState::PROCESSING:    return "PROCESSING";
        case VoicePipelineState::SPEAKING:      return "SPEAKING";
        case VoicePipelineState::ERROR:         return "ERROR";
        default:                                return "UNKNOWN";
    }
}

void voicePipelinePrintMemory() {
    Serial.printf("[%s] === Voice Pipeline Memory Usage ===\n", TAG);
    Serial.printf("[%s] scaffold: ESP-SR not linked, no voice memory allocated\n", TAG);
    Serial.printf("[%s] Expected allocation when implemented:\n", TAG);
    Serial.printf("[%s]   WakeNet9:        ~340 KB PSRAM\n", TAG);
    Serial.printf("[%s]   AFE (1ch, AEC):  ~1.1 MB PSRAM\n", TAG);
    Serial.printf("[%s]   OPUS codec:       ~50 KB internal RAM\n", TAG);
    Serial.printf("[%s]   Audio buffers:    ~64 KB PSRAM\n", TAG);
    Serial.printf("[%s]   ────────────────────────────────\n", TAG);
    Serial.printf("[%s]   Total:           ~1.5 MB\n", TAG);

    // Current system memory for reference
    Serial.printf("[%s] Current free heap:  %u bytes\n", TAG, ESP.getFreeHeap());
    Serial.printf("[%s] Current free PSRAM: %u bytes\n", TAG, ESP.getFreePsram());
}

} // namespace voice
} // namespace zacus
