#pragma once

// =============================================================================
// SCAFFOLD: Voice Pipeline for Professeur Zacus
// =============================================================================
// This header defines the voice pipeline interface for ESP-SR integration.
// It is a SCAFFOLD — the actual ESP-SR library calls are not yet wired.
//
// Prerequisites for full implementation:
//   - ESP-IDF framework (not Arduino) with esp-sr component
//   - INMP441 MEMS microphone on I2S0
//   - Custom wake word model ("Professeur Zacus") from Espressif
//   - WebSocket connection to mascarade bridge for STT/LLM/TTS
//
// Reference: XiaoZhi-ESP32 project for ESP-SR integration patterns
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace zacus {
namespace voice {

// ---------------------------------------------------------------------------
// Voice pipeline state machine
// ---------------------------------------------------------------------------

/// Voice pipeline states — follows XiaoZhi-style state machine
enum class VoicePipelineState {
    IDLE,           // Not listening, pipeline dormant
    LISTENING,      // AFE active, waiting for wake word detection
    WAKE_DETECTED,  // Wake word detected, transitioning to command/stream
    COMMAND,        // Listening for command (local MultiNet or streaming)
    STREAMING,      // Streaming audio to mascarade server via WebSocket
    PROCESSING,     // Waiting for server response (LLM inference)
    SPEAKING,       // Playing TTS response through speaker (I2S1)
    ERROR           // Error state — check logs for details
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Configuration for the voice pipeline
/// Pin defaults match the Freenove All-in-One board + INMP441 wiring
struct VoicePipelineConfig {
    // -----------------------------------------------------------------------
    // I2S microphone pins (I2S0 — separate bus from speaker on I2S1)
    // INMP441 MEMS mic wiring:
    //   INMP441 SCK  -> ESP32-S3 GPIO 41 (BCLK)
    //   INMP441 WS   -> ESP32-S3 GPIO 42 (LRCLK)
    //   INMP441 SD   -> ESP32-S3 GPIO 2  (DATA)
    //   INMP441 L/R  -> GND (left channel)
    //   INMP441 VDD  -> 3.3V
    //   INMP441 GND  -> GND
    // -----------------------------------------------------------------------
    int mic_bclk_pin = 41;
    int mic_ws_pin   = 42;
    int mic_data_pin = 2;

    // -----------------------------------------------------------------------
    // Wake word configuration
    // WakeNet9 supports custom models trained by Espressif.
    // Default: "Hi Lexin" (wn9_hilexin) as placeholder until custom
    // "Professeur Zacus" model is ordered (~2-4 weeks from Espressif).
    // -----------------------------------------------------------------------
    const char* wake_word_model = "wn9_hilexin";  // Placeholder

    // -----------------------------------------------------------------------
    // Server connection (mascarade bridge)
    // WebSocket endpoint for audio streaming / STT / LLM / TTS
    // Protocol: XiaoZhi-compatible binary frames (OPUS encoded)
    // -----------------------------------------------------------------------
    const char* server_ws_url = "ws://192.168.0.119:4001/voice";

    // -----------------------------------------------------------------------
    // Timeouts
    // -----------------------------------------------------------------------
    uint32_t command_timeout_ms = 5000;   // Max silence before ending command
    uint32_t server_timeout_ms  = 10000;  // Max wait for server response

    // -----------------------------------------------------------------------
    // Audio format (standard for ESP-SR AFE)
    // -----------------------------------------------------------------------
    int sample_rate     = 16000;  // 16kHz required by WakeNet/MultiNet
    int bits_per_sample = 16;     // 16-bit PCM
    int channels        = 1;      // Mono (single INMP441)
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Initialize the voice pipeline (call once from setup)
///
/// What this will do when implemented:
///   1. Configure I2S0 driver for INMP441 mic input
///   2. Initialize AFE (Audio Front-End) with AEC + noise suppression + VAD
///   3. Load WakeNet9 model into PSRAM
///   4. Create FreeRTOS tasks for audio capture and processing
///   5. Establish WebSocket connection to mascarade bridge
///
/// @param config  Pipeline configuration (pins, server URL, etc.)
/// @return true if initialization succeeded
///
/// NOTE: Requires ESP-IDF framework with esp-sr component.
///       This is a SCAFFOLD — returns false with a log message.
bool voicePipelineInit(const VoicePipelineConfig& config);

/// Start listening for wake word
/// Transitions from IDLE -> LISTENING
/// AFE begins processing mic input through WakeNet
void voicePipelineStart();

/// Stop listening and return to IDLE
/// Releases AFE processing but keeps I2S driver alive
void voicePipelineStop();

/// Get current pipeline state
VoicePipelineState voicePipelineGetState();

/// Callback type for voice events
/// @param state  New pipeline state
/// @param text   Associated text (wake word name, recognized command,
///               LLM response text, or error message — may be nullptr)
using VoiceEventCallback = void(*)(VoicePipelineState state, const char* text);

/// Register callback for voice pipeline state changes
/// Only one callback is supported — last registration wins
void voicePipelineOnEvent(VoiceEventCallback callback);

// ---------------------------------------------------------------------------
// Utility / Debug
// ---------------------------------------------------------------------------

/// Get human-readable name for a pipeline state (for logging)
const char* voicePipelineStateName(VoicePipelineState state);

/// Print current memory usage related to voice pipeline
/// (PSRAM allocation for WakeNet, AFE, OPUS buffers)
void voicePipelinePrintMemory();

} // namespace voice
} // namespace zacus
