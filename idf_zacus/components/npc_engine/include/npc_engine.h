// Zacus npc_engine — ESP-IDF C port of the Arduino NPC decision engine.
//
// Source of truth for the Arduino implementation:
//   ESP32_ZACUS/ui_freenove_allinone/src/npc/npc_engine.cpp
//   ESP32_ZACUS/ui_freenove_allinone/include/npc/npc_engine.h
//
// The IDF port keeps the Arduino "core" state-machine API verbatim
// (npc_init / npc_evaluate / npc_on_*) because that code was already
// pure-C, side-effect free and free of Arduino-runtime dependencies.
//
// On top of that core the port adds an IDF-idiomatic "engine" wrapper
// layer with:
//   * npc_engine_init(config)        — boot the singleton, log readiness
//   * npc_engine_update(now_ms)      — periodic tick (mood + auto-evaluate)
//   * npc_engine_trigger_cue(cue_id) — best-effort cue dispatch through
//                                       media_manager_play()
//   * npc_engine_set_step(step_id)   — bridge to the scenario engine
//   * npc_engine_request_hint(...)   — async hint request (stubbed locally
//                                       until the hints-engine HTTP client
//                                       lands in a later slice)
//
// All wrapper entry points return `esp_err_t`. Callbacks are plain C
// function pointers — no C++ classes, no lambdas, RTOS-friendly.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Core (ported verbatim from the Arduino sources) ─────────────────────────

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
    uint8_t  current_scene;
    uint8_t  current_step;
    uint32_t scene_start_ms;
    uint32_t total_elapsed_ms;
    uint8_t  hints_given[NPC_MAX_SCENES];
    uint8_t  qr_scanned_count;
    uint8_t  failed_attempts;
    bool     phone_off_hook;
    bool     tower_reachable;
    npc_mood_t mood;
    uint32_t last_qr_scan_ms;
    uint32_t expected_scene_duration_ms;
} npc_state_t;

typedef struct {
    npc_trigger_t      trigger;
    npc_audio_source_t audio_source;
    char               phrase_text[NPC_PHRASE_MAX_LEN];
    char               sd_path[128];
    npc_mood_t         resulting_mood;
} npc_decision_t;

void npc_init(npc_state_t *state);
void npc_reset(npc_state_t *state);
bool npc_evaluate(const npc_state_t *state, uint32_t now_ms,
                  npc_decision_t *out);
void npc_on_scene_change(npc_state_t *state, uint8_t new_scene,
                         uint32_t expected_duration_ms, uint32_t now_ms);
void npc_on_qr_scan(npc_state_t *state, bool valid, uint32_t now_ms);
void npc_on_phone_hook(npc_state_t *state, bool off_hook);
void npc_on_hint_request(npc_state_t *state, uint32_t now_ms);
void npc_on_tower_status(npc_state_t *state, bool reachable);
void npc_update_mood(npc_state_t *state, uint32_t now_ms);
uint8_t npc_hint_level(const npc_state_t *state, uint8_t scene);
bool npc_build_sd_path(char *out_path, size_t capacity,
                       uint8_t scene, npc_trigger_t trigger,
                       npc_mood_t mood, uint8_t variant);

// ── Engine wrapper (IDF idioms) ─────────────────────────────────────────────

#define NPC_ENGINE_MAX_CUES        32
#define NPC_ENGINE_CUE_PATH_MAX    128
#define NPC_ENGINE_CUE_ID_MAX      32

// Static cue table entry. Authored cues live in flash; runtime state
// (already-played flag, cooldown) is tracked separately in RAM.
typedef struct {
    char     id[NPC_ENGINE_CUE_ID_MAX];
    char     audio_path[NPC_ENGINE_CUE_PATH_MAX];
    uint8_t  scene;          // associated scene index (0xFF = global cue)
    npc_mood_t mood;
} npc_cue_t;

// Configuration for the wrapper. `cues` may be NULL/0 — the engine still
// boots and accepts triggers (each `trigger_cue` call simply tries
// media_manager_play() with the supplied cue identifier as a path).
typedef struct {
    const npc_cue_t *cues;
    size_t           cue_count;
    bool             auto_evaluate;            // run npc_evaluate() each tick
    bool             auto_play_decisions;      // dispatch decisions through media_manager_play
} npc_engine_config_t;

// Hint request callback. Invoked when the hints-engine produced a result
// (today: synchronously with a hardcoded stub text). `text` is owned by
// the engine and only valid for the duration of the call — copy if needed.
typedef void (*npc_hint_callback_t)(uint8_t puzzle_id, uint8_t level,
                                    esp_err_t status, const char *text,
                                    void *user_ctx);

// Initialize the engine singleton. `config` may be NULL — defaults are
// applied (no cue table, auto_evaluate=false, auto_play_decisions=false).
esp_err_t npc_engine_init(const npc_engine_config_t *config);

// Periodic tick. `now_ms` is the same monotonic millisecond counter as
// `media_manager_update`. Updates mood, optionally runs npc_evaluate and
// dispatches the resulting cue when `auto_*` flags are enabled.
esp_err_t npc_engine_update(uint32_t now_ms);

// Manually trigger a cue by id. Looks up the cue table and dispatches the
// associated audio path through media_manager_play(). When the cue is not
// in the table the engine treats `cue_id` itself as the path and forwards
// it as-is — useful for ad-hoc tests from the REST surface.
//
// Returns:
//   ESP_OK              cue dispatched (media_manager_play returned OK)
//   ESP_ERR_NOT_FOUND   cue id not in table AND raw path not playable
//   ESP_ERR_INVALID_*   propagated from media_manager
esp_err_t npc_engine_trigger_cue(const char *cue_id);

// Bridge from the scenario runtime — informs the engine of the active
// step (and implicitly the active scene). Resets failed-attempts counter
// and primes the stuck timer.
esp_err_t npc_engine_set_step(uint8_t step_id, uint32_t expected_duration_ms);

// Request a hint for `puzzle_id` at escalation `level` (0..3). The engine
// invokes `cb` with the resulting text. The current implementation is a
// LOCAL STUB: it returns a hardcoded French placeholder synchronously.
// TODO(slice-6): replace with HTTP POST to /hints/ask on the hints engine.
esp_err_t npc_engine_request_hint(uint8_t puzzle_id, uint8_t level,
                                  npc_hint_callback_t cb, void *user_ctx);

// Read-only access to the underlying core state — handy for diagnostics.
const npc_state_t *npc_engine_state(void);

#ifdef __cplusplus
}
#endif
