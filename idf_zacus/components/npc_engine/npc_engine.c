// Zacus npc_engine — ESP-IDF C port (slice 4, P1).
//
// Portage strategy: the Arduino sources were already pure C (no classes,
// no Arduino runtime calls), so the "core" half of this file is a verbatim
// copy of ui_freenove_allinone/src/npc/npc_engine.cpp with `cstring`/`cstdio`
// replaced by their C headers. The "engine wrapper" half is new and follows
// the same pattern as the media_manager component:
//
//   * single static singleton, idempotent init
//   * esp_err_t returns + ESP_LOG instrumentation
//   * media_manager_play() integration for cue dispatch
//
// Hint requests are stubbed locally (synchronous callback with a hardcoded
// French placeholder). The real HTTP call to the hints engine lands in a
// later slice (see TODO below).

#include "npc_engine.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "media_manager.h"

static const char *TAG = "npc_engine";

// ─── Core: scene/trigger/mood lookup tables (verbatim Arduino) ──────────────

static const char *const kSceneIds[] = {
    "SCENE_U_SON_PROTO",
    "SCENE_LA_DETECTOR",
    "SCENE_WIN_ETAPE1",
    "SCENE_WARNING",
    "SCENE_LEFOU_DETECTOR",
    "SCENE_WIN_ETAPE2",
    "SCENE_QR_DETECTOR",
    "SCENE_FINAL_WIN",
};
static const uint8_t kSceneCount = sizeof(kSceneIds) / sizeof(kSceneIds[0]);

static const char *const kTriggerDirs[] = {
    [NPC_TRIGGER_NONE]             = "generic",
    [NPC_TRIGGER_HINT_REQUEST]     = "indice",
    [NPC_TRIGGER_STUCK_TIMER]      = "indice",
    [NPC_TRIGGER_QR_SCANNED]       = "felicitations",
    [NPC_TRIGGER_WRONG_ACTION]     = "attention",
    [NPC_TRIGGER_FAST_PROGRESS]    = "fausse_piste",
    [NPC_TRIGGER_SLOW_PROGRESS]    = "adaptation",
    [NPC_TRIGGER_SCENE_TRANSITION] = "transition",
    [NPC_TRIGGER_GAME_START]       = "ambiance",
    [NPC_TRIGGER_GAME_END]         = "ambiance",
};

static const char *const kMoodSuffixes[] = {
    [NPC_MOOD_NEUTRAL]   = "neutral",
    [NPC_MOOD_IMPRESSED] = "impressed",
    [NPC_MOOD_WORRIED]   = "worried",
    [NPC_MOOD_AMUSED]    = "amused",
};

// ─── Core: state-machine API (verbatim Arduino, NULL guards preserved) ──────

void npc_init(npc_state_t *state) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->mood = NPC_MOOD_NEUTRAL;
}

void npc_reset(npc_state_t *state) {
    npc_init(state);
}

void npc_on_scene_change(npc_state_t *state, uint8_t new_scene,
                         uint32_t expected_duration_ms, uint32_t now_ms) {
    if (state == NULL) return;
    state->current_scene = new_scene;
    state->scene_start_ms = now_ms;
    state->expected_scene_duration_ms = expected_duration_ms;
    state->failed_attempts = 0;
}

void npc_on_qr_scan(npc_state_t *state, bool valid, uint32_t now_ms) {
    if (state == NULL) return;
    if (valid) {
        state->qr_scanned_count++;
    } else {
        state->failed_attempts++;
    }
    state->last_qr_scan_ms = now_ms;
}

void npc_on_phone_hook(npc_state_t *state, bool off_hook) {
    if (state == NULL) return;
    state->phone_off_hook = off_hook;
}

void npc_on_hint_request(npc_state_t *state, uint32_t now_ms) {
    if (state == NULL) return;
    uint8_t scene = state->current_scene;
    if (scene < NPC_MAX_SCENES && state->hints_given[scene] < NPC_MAX_HINT_LEVEL) {
        state->hints_given[scene]++;
    }
    (void) now_ms;
}

void npc_on_tower_status(npc_state_t *state, bool reachable) {
    if (state == NULL) return;
    state->tower_reachable = reachable;
}

void npc_update_mood(npc_state_t *state, uint32_t now_ms) {
    if (state == NULL || state->expected_scene_duration_ms == 0) return;
    uint32_t elapsed = now_ms - state->scene_start_ms;
    uint32_t expected = state->expected_scene_duration_ms;
    uint32_t pct = (elapsed * 100U) / expected;

    if (state->failed_attempts >= 3) {
        state->mood = NPC_MOOD_AMUSED;
    } else if (pct < NPC_FAST_THRESHOLD_PCT) {
        state->mood = NPC_MOOD_IMPRESSED;
    } else if (pct > NPC_SLOW_THRESHOLD_PCT) {
        state->mood = NPC_MOOD_WORRIED;
    } else {
        state->mood = NPC_MOOD_NEUTRAL;
    }
}

uint8_t npc_hint_level(const npc_state_t *state, uint8_t scene) {
    if (state == NULL || scene >= NPC_MAX_SCENES) return 0;
    return state->hints_given[scene];
}

bool npc_build_sd_path(char *out_path, size_t capacity,
                       uint8_t scene, npc_trigger_t trigger,
                       npc_mood_t mood, uint8_t variant) {
    if (out_path == NULL || capacity < 16) return false;

    const char *scene_id = (scene < kSceneCount) ? kSceneIds[scene] : "npc";
    const char *trigger_dir = (trigger < NPC_TRIGGER_COUNT)
        ? kTriggerDirs[trigger] : "generic";
    const char *mood_str = (mood < NPC_MOOD_COUNT)
        ? kMoodSuffixes[mood] : "neutral";

    bool is_scene_specific = (trigger != NPC_TRIGGER_GAME_START
                           && trigger != NPC_TRIGGER_GAME_END
                           && trigger != NPC_TRIGGER_NONE);

    int written;
    if (is_scene_specific && scene < kSceneCount) {
        written = snprintf(out_path, capacity,
            "/hotline_tts/%s/%s_%s_%u.mp3",
            scene_id, trigger_dir, mood_str, (unsigned) variant);
    } else {
        written = snprintf(out_path, capacity,
            "/hotline_tts/npc/%s_%s_%u.mp3",
            trigger_dir, mood_str, (unsigned) variant);
    }
    return (written > 0 && (size_t) written < capacity);
}

bool npc_evaluate(const npc_state_t *state, uint32_t now_ms,
                  npc_decision_t *out) {
    if (state == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));

    uint32_t scene_elapsed = now_ms - state->scene_start_ms;
    uint32_t expected = state->expected_scene_duration_ms;

    // Priority 1: Hint request (phone off hook while stuck)
    if (state->phone_off_hook && scene_elapsed > NPC_STUCK_TIMEOUT_MS) {
        uint8_t level = npc_hint_level(state, state->current_scene);
        out->trigger = NPC_TRIGGER_HINT_REQUEST;
        out->resulting_mood = state->mood;
        npc_build_sd_path(out->sd_path, sizeof(out->sd_path),
                          state->current_scene, NPC_TRIGGER_HINT_REQUEST,
                          state->mood, level);
        out->audio_source = state->tower_reachable
            ? NPC_AUDIO_LIVE_TTS : NPC_AUDIO_SD_CONTEXTUAL;
        return true;
    }

    // Priority 2: Stuck timer (proactive, no phone needed)
    if (scene_elapsed > NPC_STUCK_TIMEOUT_MS
        && npc_hint_level(state, state->current_scene) == 0) {
        out->trigger = NPC_TRIGGER_STUCK_TIMER;
        out->resulting_mood = NPC_MOOD_WORRIED;
        npc_build_sd_path(out->sd_path, sizeof(out->sd_path),
                          state->current_scene, NPC_TRIGGER_STUCK_TIMER,
                          NPC_MOOD_WORRIED, 0);
        out->audio_source = state->tower_reachable
            ? NPC_AUDIO_LIVE_TTS : NPC_AUDIO_SD_CONTEXTUAL;
        return true;
    }

    // Priority 3: Fast progress detection
    if (expected > 0 && scene_elapsed > 0
        && (scene_elapsed * 100U / expected) < NPC_FAST_THRESHOLD_PCT) {
        out->trigger = NPC_TRIGGER_FAST_PROGRESS;
        out->resulting_mood = NPC_MOOD_IMPRESSED;
        npc_build_sd_path(out->sd_path, sizeof(out->sd_path),
                          state->current_scene, NPC_TRIGGER_FAST_PROGRESS,
                          NPC_MOOD_IMPRESSED, 0);
        out->audio_source = state->tower_reachable
            ? NPC_AUDIO_LIVE_TTS : NPC_AUDIO_SD_CONTEXTUAL;
        return true;
    }

    // Priority 4: Slow progress detection
    if (expected > 0 && (scene_elapsed * 100U / expected) > NPC_SLOW_THRESHOLD_PCT) {
        out->trigger = NPC_TRIGGER_SLOW_PROGRESS;
        out->resulting_mood = NPC_MOOD_WORRIED;
        npc_build_sd_path(out->sd_path, sizeof(out->sd_path),
                          state->current_scene, NPC_TRIGGER_SLOW_PROGRESS,
                          NPC_MOOD_WORRIED, 0);
        out->audio_source = state->tower_reachable
            ? NPC_AUDIO_LIVE_TTS : NPC_AUDIO_SD_CONTEXTUAL;
        return true;
    }

    return false;
}

// ─── Wrapper singleton (slice-4 IDF surface) ────────────────────────────────

typedef struct {
    bool                ready;
    npc_state_t         core;
    const npc_cue_t    *cues;
    size_t              cue_count;
    bool                played[NPC_ENGINE_MAX_CUES];   // already-played log
    bool                auto_evaluate;
    bool                auto_play_decisions;
} engine_t;

static engine_t s_engine;

static const npc_cue_t *find_cue(const char *cue_id) {
    if (cue_id == NULL || s_engine.cues == NULL) return NULL;
    for (size_t i = 0; i < s_engine.cue_count && i < NPC_ENGINE_MAX_CUES; ++i) {
        if (strncmp(s_engine.cues[i].id, cue_id,
                    NPC_ENGINE_CUE_ID_MAX) == 0) {
            return &s_engine.cues[i];
        }
    }
    return NULL;
}

static size_t cue_index(const npc_cue_t *cue) {
    if (cue == NULL || s_engine.cues == NULL) return SIZE_MAX;
    return (size_t) (cue - s_engine.cues);
}

esp_err_t npc_engine_init(const npc_engine_config_t *config) {
    memset(&s_engine, 0, sizeof(s_engine));
    npc_init(&s_engine.core);

    if (config != NULL) {
        s_engine.cues                = config->cues;
        s_engine.cue_count           = config->cue_count;
        s_engine.auto_evaluate       = config->auto_evaluate;
        s_engine.auto_play_decisions = config->auto_play_decisions;

        if (s_engine.cue_count > NPC_ENGINE_MAX_CUES) {
            ESP_LOGW(TAG, "cue table truncated: %u > NPC_ENGINE_MAX_CUES (%u)",
                     (unsigned) s_engine.cue_count,
                     (unsigned) NPC_ENGINE_MAX_CUES);
            s_engine.cue_count = NPC_ENGINE_MAX_CUES;
        }
    }

    s_engine.ready = true;
    ESP_LOGI(TAG, "npc_engine ready, %u cues registered "
                  "(auto_evaluate=%d, auto_play=%d)",
             (unsigned) s_engine.cue_count,
             (int) s_engine.auto_evaluate,
             (int) s_engine.auto_play_decisions);
    return ESP_OK;
}

esp_err_t npc_engine_update(uint32_t now_ms) {
    if (!s_engine.ready) return ESP_ERR_INVALID_STATE;

    s_engine.core.total_elapsed_ms = now_ms;
    npc_update_mood(&s_engine.core, now_ms);

    if (!s_engine.auto_evaluate) return ESP_OK;

    npc_decision_t decision;
    if (!npc_evaluate(&s_engine.core, now_ms, &decision)) return ESP_OK;

    ESP_LOGI(TAG, "decision: trigger=%d mood=%d audio=%d path=\"%s\"",
             (int) decision.trigger,
             (int) decision.resulting_mood,
             (int) decision.audio_source,
             decision.sd_path);

    if (s_engine.auto_play_decisions && decision.sd_path[0] != '\0') {
        esp_err_t err = media_manager_play(decision.sd_path);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "auto-play \"%s\" failed: %s",
                     decision.sd_path, esp_err_to_name(err));
        }
    }
    return ESP_OK;
}

esp_err_t npc_engine_trigger_cue(const char *cue_id) {
    if (!s_engine.ready) return ESP_ERR_INVALID_STATE;
    if (cue_id == NULL || cue_id[0] == '\0') return ESP_ERR_INVALID_ARG;

    const npc_cue_t *cue = find_cue(cue_id);
    const char *path = NULL;
    size_t idx = SIZE_MAX;

    if (cue != NULL) {
        path = cue->audio_path;
        idx  = cue_index(cue);
        ESP_LOGI(TAG, "trigger_cue id=\"%s\" -> path=\"%s\" (scene=%u, mood=%d)",
                 cue_id, path, (unsigned) cue->scene, (int) cue->mood);
    } else {
        // Fallback: treat the id itself as a path. Useful for ad-hoc REST tests.
        path = cue_id;
        ESP_LOGI(TAG, "trigger_cue id=\"%s\" not in table — playing raw path",
                 cue_id);
    }

    esp_err_t err = media_manager_play(path);
    if (err == ESP_OK && idx < NPC_ENGINE_MAX_CUES) {
        s_engine.played[idx] = true;
    }
    return err;
}

esp_err_t npc_engine_set_step(uint8_t step_id, uint32_t expected_duration_ms) {
    if (!s_engine.ready) return ESP_ERR_INVALID_STATE;

    s_engine.core.current_step = step_id;
    npc_on_scene_change(&s_engine.core, step_id, expected_duration_ms,
                        s_engine.core.total_elapsed_ms);
    ESP_LOGI(TAG, "step set to %u (expected_duration=%u ms)",
             (unsigned) step_id, (unsigned) expected_duration_ms);
    return ESP_OK;
}

esp_err_t npc_engine_request_hint(uint8_t puzzle_id, uint8_t level,
                                  npc_hint_callback_t cb, void *user_ctx) {
    if (!s_engine.ready) return ESP_ERR_INVALID_STATE;
    if (cb == NULL) return ESP_ERR_INVALID_ARG;

    // TODO(slice-6): replace with HTTP POST to hints engine /hints/ask.
    // For now we synchronously deliver a hardcoded French placeholder so
    // the surrounding NPC orchestration can be wired and tested end-to-end.
    static const char *const kStubHints[NPC_MAX_HINT_LEVEL + 1] = {
        "Regarde autour de toi, la solution est plus proche que tu ne crois.",
        "As-tu pensé à observer chaque indice plus attentivement ?",
        "Concentre-toi sur l'objet le plus inhabituel de la pièce.",
        "Le code se trouve dans la séquence des couleurs, dans l'ordre.",
    };
    const uint8_t clamped = (level > NPC_MAX_HINT_LEVEL)
        ? NPC_MAX_HINT_LEVEL : level;
    const char *text = kStubHints[clamped];

    ESP_LOGI(TAG, "hint request puzzle=%u level=%u -> stub \"%s\"",
             (unsigned) puzzle_id, (unsigned) level, text);

    npc_on_hint_request(&s_engine.core, s_engine.core.total_elapsed_ms);
    cb(puzzle_id, clamped, ESP_OK, text, user_ctx);
    return ESP_OK;
}

const npc_state_t *npc_engine_state(void) {
    return s_engine.ready ? &s_engine.core : NULL;
}
