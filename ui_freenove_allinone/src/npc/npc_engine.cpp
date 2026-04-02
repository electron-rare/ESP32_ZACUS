// npc_engine.cpp - Professor Zacus NPC decision engine implementation.
#include "npc/npc_engine.h"
#include <cstring>
#include <cstdio>

// Scene ID strings for SD path generation (must match zacus_v2.yaml step order).
static const char* const kSceneIds[] = {
    "SCENE_U_SON_PROTO",
    "SCENE_LA_DETECTOR",
    "SCENE_WIN_ETAPE1",
    "SCENE_WARNING",
    "SCENE_LEFOU_DETECTOR",
    "SCENE_WIN_ETAPE2",
    "SCENE_QR_DETECTOR",
    "SCENE_FINAL_WIN"
};
static const uint8_t kSceneCount = sizeof(kSceneIds) / sizeof(kSceneIds[0]);

static const char* const kTriggerDirs[] = {
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

static const char* const kMoodSuffixes[] = {
    [NPC_MOOD_NEUTRAL]    = "neutral",
    [NPC_MOOD_IMPRESSED]  = "impressed",
    [NPC_MOOD_WORRIED]    = "worried",
    [NPC_MOOD_AMUSED]     = "amused",
};

void npc_init(npc_state_t* state) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->mood = NPC_MOOD_NEUTRAL;
}

void npc_reset(npc_state_t* state) {
    npc_init(state);
}

void npc_on_scene_change(npc_state_t* state, uint8_t new_scene,
                         uint32_t expected_duration_ms, uint32_t now_ms) {
    if (state == NULL) return;
    state->current_scene = new_scene;
    state->scene_start_ms = now_ms;
    state->expected_scene_duration_ms = expected_duration_ms;
    state->failed_attempts = 0;
}

void npc_on_qr_scan(npc_state_t* state, bool valid, uint32_t now_ms) {
    if (state == NULL) return;
    if (valid) {
        state->qr_scanned_count++;
    } else {
        state->failed_attempts++;
    }
    state->last_qr_scan_ms = now_ms;
}

void npc_on_phone_hook(npc_state_t* state, bool off_hook) {
    if (state == NULL) return;
    state->phone_off_hook = off_hook;
}

void npc_on_hint_request(npc_state_t* state, uint32_t now_ms) {
    if (state == NULL) return;
    uint8_t scene = state->current_scene;
    if (scene < NPC_MAX_SCENES && state->hints_given[scene] < NPC_MAX_HINT_LEVEL) {
        state->hints_given[scene]++;
    }
    (void)now_ms;
}

void npc_on_tower_status(npc_state_t* state, bool reachable) {
    if (state == NULL) return;
    state->tower_reachable = reachable;
}

void npc_update_mood(npc_state_t* state, uint32_t now_ms) {
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

uint8_t npc_hint_level(const npc_state_t* state, uint8_t scene) {
    if (state == NULL || scene >= NPC_MAX_SCENES) return 0;
    return state->hints_given[scene];
}

bool npc_build_sd_path(char* out_path, size_t capacity,
                       uint8_t scene, npc_trigger_t trigger,
                       npc_mood_t mood, uint8_t variant) {
    if (out_path == NULL || capacity < 16) return false;

    const char* scene_id = (scene < kSceneCount) ? kSceneIds[scene] : "npc";
    const char* trigger_dir = (trigger < NPC_TRIGGER_COUNT)
        ? kTriggerDirs[trigger] : "generic";
    const char* mood_str = (mood < NPC_MOOD_COUNT)
        ? kMoodSuffixes[mood] : "neutral";

    // Per-scene triggers: /hotline_tts/{scene_id}/{trigger}_{mood}_{variant}.mp3
    // Generic NPC: /hotline_tts/npc/{trigger}_{mood}_{variant}.mp3
    bool is_scene_specific = (trigger != NPC_TRIGGER_GAME_START
                           && trigger != NPC_TRIGGER_GAME_END
                           && trigger != NPC_TRIGGER_NONE);

    int written;
    if (is_scene_specific && scene < kSceneCount) {
        written = snprintf(out_path, capacity,
            "/hotline_tts/%s/%s_%s_%u.mp3",
            scene_id, trigger_dir, mood_str, (unsigned)variant);
    } else {
        written = snprintf(out_path, capacity,
            "/hotline_tts/npc/%s_%s_%u.mp3",
            trigger_dir, mood_str, (unsigned)variant);
    }
    return (written > 0 && (size_t)written < capacity);
}

bool npc_evaluate(const npc_state_t* state, uint32_t now_ms, npc_decision_t* out) {
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
