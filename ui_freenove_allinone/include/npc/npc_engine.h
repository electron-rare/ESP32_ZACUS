// npc_engine.h - Professor Zacus NPC decision engine.
// Lightweight state machine: trigger rules, mood system, phrase selection.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NPC_MAX_SCENES        12
#define NPC_MAX_HINT_LEVEL    3
#define NPC_PHRASE_MAX_LEN    200
#define NPC_STUCK_TIMEOUT_MS  (3UL * 60UL * 1000UL)
#define NPC_FAST_THRESHOLD_PCT  50
#define NPC_SLOW_THRESHOLD_PCT  150
#define NPC_QR_DEBOUNCE_MS    30000

typedef enum {
    NPC_MOOD_NEUTRAL = 0,
    NPC_MOOD_IMPRESSED,
    NPC_MOOD_WORRIED,
    NPC_MOOD_AMUSED,
    NPC_MOOD_COUNT
} npc_mood_t;

typedef enum {
    NPC_TRIGGER_NONE = 0,
    NPC_TRIGGER_HINT_REQUEST,
    NPC_TRIGGER_STUCK_TIMER,
    NPC_TRIGGER_QR_SCANNED,
    NPC_TRIGGER_WRONG_ACTION,
    NPC_TRIGGER_FAST_PROGRESS,
    NPC_TRIGGER_SLOW_PROGRESS,
    NPC_TRIGGER_SCENE_TRANSITION,
    NPC_TRIGGER_GAME_START,
    NPC_TRIGGER_GAME_END,
    NPC_TRIGGER_COUNT
} npc_trigger_t;

typedef enum {
    NPC_AUDIO_NONE = 0,
    NPC_AUDIO_LIVE_TTS,
    NPC_AUDIO_SD_CONTEXTUAL,
    NPC_AUDIO_SD_GENERIC
} npc_audio_source_t;

typedef struct {
    uint8_t current_scene;
    uint8_t current_step;
    uint32_t scene_start_ms;
    uint32_t total_elapsed_ms;
    uint8_t hints_given[NPC_MAX_SCENES];
    uint8_t qr_scanned_count;
    uint8_t failed_attempts;
    bool phone_off_hook;
    bool tower_reachable;
    npc_mood_t mood;
    uint32_t last_qr_scan_ms;
    uint32_t expected_scene_duration_ms;
} npc_state_t;

typedef struct {
    npc_trigger_t trigger;
    npc_audio_source_t audio_source;
    char phrase_text[NPC_PHRASE_MAX_LEN];
    char sd_path[128];
    npc_mood_t resulting_mood;
} npc_decision_t;

/// Initialize NPC state to defaults.
void npc_init(npc_state_t* state);

/// Reset NPC state for a new game session.
void npc_reset(npc_state_t* state);

/// Evaluate trigger rules and produce a decision.
/// Returns true if NPC wants to speak.
bool npc_evaluate(const npc_state_t* state, uint32_t now_ms, npc_decision_t* out);

/// Notify NPC of a scene change.
void npc_on_scene_change(npc_state_t* state, uint8_t new_scene,
                         uint32_t expected_duration_ms, uint32_t now_ms);

/// Notify NPC of a QR scan result.
void npc_on_qr_scan(npc_state_t* state, bool valid, uint32_t now_ms);

/// Notify NPC that player picked up / hung up phone.
void npc_on_phone_hook(npc_state_t* state, bool off_hook);

/// Notify NPC of hint request (phone picked up while stuck).
void npc_on_hint_request(npc_state_t* state, uint32_t now_ms);

/// Notify NPC of Tower TTS reachability change.
void npc_on_tower_status(npc_state_t* state, bool reachable);

/// Update mood based on progress ratio (elapsed_ms / expected_ms).
void npc_update_mood(npc_state_t* state, uint32_t now_ms);

/// Get the current hint level for a scene (0 = no hints given, max 3).
uint8_t npc_hint_level(const npc_state_t* state, uint8_t scene);

/// Build SD card fallback path for a given trigger + scene.
/// Writes to out_path, returns true if a valid path was built.
bool npc_build_sd_path(char* out_path, size_t capacity,
                       uint8_t scene, npc_trigger_t trigger,
                       npc_mood_t mood, uint8_t variant);

#ifdef __cplusplus
}
#endif
