// npc_v3.cpp — V3 Adaptive NPC logic: group profiling, parcours selection,
// duration targeting. Companion to npc_engine.cpp.
#include "npc/npc_v3.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "NPC_V3";

// ============================================================
// Profiling thresholds (from v3_constants.yaml)
// ============================================================
#define V3_FAST_THRESHOLD_MS   (180U * 1000U)   // < 3 min = fast on profiling puzzle
#define V3_SLOW_THRESHOLD_MS   (480U * 1000U)   // > 8 min = slow on profiling puzzle
#define V3_FAST_SCENE_PCT      60U               // < 60% of expected scene time = fast overall
#define V3_SLOW_SCENE_PCT      130U              // > 130% = slow overall
#define V3_SKIP_SCENE_PCT      160U              // > 160% = skip optional puzzle
#define V3_DURATION_WARN_PCT   80U               // 80% of target_duration → warning
#define V3_MAX_BONUS_PUZZLES   2U

// ============================================================
// Parcours definitions per group profile
// P7_COFFRE (id=7) is always last — added by game_coordinator
// ============================================================

// TECH: P2, P4, P5, P3 → +P7 climax
static const uint8_t kParcoursTech[]    = {2, 4, 5, 3, 7};
static const uint8_t kParcoursLenTech   = 5;

// NON_TECH: P1, P6, P3, P5 → +P7 climax
static const uint8_t kParcoursNonTech[] = {1, 6, 3, 5, 7};
static const uint8_t kParcoursLenNonTech = 5;

// MIXED: P1, P2, P3, P5, P6 → +P7 climax
static const uint8_t kParcoursMixed[]   = {1, 2, 3, 5, 6, 7};
static const uint8_t kParcoursLenMixed  = 6;

// ============================================================
// npc_v3_init
// ============================================================

void npc_v3_init(npc_v3_state_t* v3, uint32_t target_duration_ms, uint32_t now_ms) {
    if (!v3) return;
    memset(v3, 0, sizeof(*v3));
    v3->group_profile        = GROUP_UNKNOWN;
    v3->target_duration_ms   = target_duration_ms;
    v3->game_start_ms        = now_ms;
    v3->total_puzzles        = kParcoursLenMixed;  // default until profile determined
    ESP_LOGI(TAG, "init target=%lu ms", (unsigned long)target_duration_ms);
}

// ============================================================
// npc_v3_on_profiling_solved
// ============================================================

void npc_v3_on_profiling_solved(npc_v3_state_t* v3, uint8_t puzzle_id,
                                 uint32_t solve_time_ms) {
    if (!v3) return;
    if (puzzle_id == 1) {
        v3->profiling_p1_time_ms = solve_time_ms;
        ESP_LOGI(TAG, "P1 profiling solve_time=%lu ms", (unsigned long)solve_time_ms);
    } else if (puzzle_id == 2) {
        v3->profiling_p2_time_ms = solve_time_ms;
        ESP_LOGI(TAG, "P2 profiling solve_time=%lu ms", (unsigned long)solve_time_ms);
    }
    v3->puzzles_solved++;
}

// ============================================================
// npc_v3_classify_group
// ============================================================

group_profile_t npc_v3_classify_group(const npc_v3_state_t* v3) {
    if (!v3) return GROUP_UNKNOWN;
    if (v3->profiling_p1_time_ms == 0 || v3->profiling_p2_time_ms == 0) {
        return GROUP_UNKNOWN;
    }

    bool p1_fast = (v3->profiling_p1_time_ms < V3_FAST_THRESHOLD_MS);
    bool p2_fast = (v3->profiling_p2_time_ms < V3_FAST_THRESHOLD_MS);
    bool p2_slow = (v3->profiling_p2_time_ms > V3_SLOW_THRESHOLD_MS);

    group_profile_t profile;
    if (p2_fast && !p1_fast) {
        profile = GROUP_TECH;
    } else if (p1_fast && p2_slow) {
        profile = GROUP_NON_TECH;
    } else {
        profile = GROUP_MIXED;
    }

    ESP_LOGI(TAG, "classify: p1=%lu p2=%lu → profile=%d",
             (unsigned long)v3->profiling_p1_time_ms,
             (unsigned long)v3->profiling_p2_time_ms,
             (int)profile);
    return profile;
}

// ============================================================
// npc_v3_select_puzzles
// ============================================================

uint8_t npc_v3_select_puzzles(group_profile_t profile,
                               uint8_t out_puzzles[NPC_V3_MAX_PARCOURS]) {
    const uint8_t* src = kParcoursMixed;
    uint8_t len        = kParcoursLenMixed;

    if (profile == GROUP_TECH) {
        src = kParcoursTech;
        len = kParcoursLenTech;
    } else if (profile == GROUP_NON_TECH) {
        src = kParcoursNonTech;
        len = kParcoursLenNonTech;
    }

    memcpy(out_puzzles, src, len);
    ESP_LOGI(TAG, "select_puzzles profile=%d len=%d", (int)profile, (int)len);
    return len;
}

// ============================================================
// npc_v3_check_duration
// ============================================================

bool npc_v3_check_duration(npc_v3_state_t* v3, uint32_t now_ms,
                            npc_v3_decision_t* out) {
    if (!v3 || !out) return false;
    if (v3->duration_warning_sent) return false;
    if (v3->target_duration_ms == 0) return false;

    uint32_t elapsed = now_ms - v3->game_start_ms;
    uint32_t threshold = (v3->target_duration_ms * V3_DURATION_WARN_PCT) / 100U;

    // Warn only when at least one puzzle remains
    if (elapsed > threshold && v3->puzzles_solved < v3->total_puzzles - 1U) {
        memset(out, 0, sizeof(*out));
        out->action = NPC_ACTION_DURATION_WARN;
        snprintf(out->phrase_key, sizeof(out->phrase_key),
                 "adaptation.duration_warning.0");
        v3->duration_warning_sent = true;
        ESP_LOGW(TAG, "duration warning: elapsed=%lu threshold=%lu",
                 (unsigned long)elapsed, (unsigned long)threshold);
        return true;
    }
    return false;
}

// ============================================================
// npc_v3_on_puzzle_solved
// ============================================================

void npc_v3_on_puzzle_solved(npc_v3_state_t* v3, uint8_t puzzle_id) {
    if (!v3) return;
    v3->puzzles_solved++;
    ESP_LOGI(TAG, "puzzle P%d solved, total=%d/%d",
             (int)puzzle_id, (int)v3->puzzles_solved, (int)v3->total_puzzles);
}

// ============================================================
// npc_v3_evaluate — main decision function
// ============================================================

bool npc_v3_evaluate(npc_v3_state_t* v3, const npc_state_t* base,
                     uint32_t now_ms, npc_v3_decision_t* out) {
    if (!v3 || !base || !out) return false;
    memset(out, 0, sizeof(*out));
    out->action = NPC_ACTION_NONE;

    // 1. Duration warning (highest priority)
    if (npc_v3_check_duration(v3, now_ms, out)) {
        return true;
    }

    // 2. Classify group profile after both profiling puzzles solved
    if (v3->group_profile == GROUP_UNKNOWN
        && v3->profiling_p1_time_ms > 0
        && v3->profiling_p2_time_ms > 0) {
        group_profile_t profile = npc_v3_classify_group(v3);
        out->action      = NPC_ACTION_SET_PROFILE;
        out->new_profile = profile;
        if (profile == GROUP_TECH) {
            snprintf(out->phrase_key, sizeof(out->phrase_key),
                     "adaptation.group_tech_detected.0");
        } else if (profile == GROUP_NON_TECH) {
            snprintf(out->phrase_key, sizeof(out->phrase_key),
                     "adaptation.group_non_tech_detected.0");
        } else {
            snprintf(out->phrase_key, sizeof(out->phrase_key),
                     "adaptation.group_mixed_detected.0");
        }
        return true;
    }

    // 3. Adaptive pacing: check current scene progress
    if (base->expected_scene_duration_ms > 0) {
        uint32_t scene_elapsed = now_ms - base->scene_start_ms;
        uint32_t pct = (scene_elapsed * 100U) / base->expected_scene_duration_ms;

        // Fast group: add bonus puzzle
        if (pct < V3_FAST_SCENE_PCT
            && v3->bonus_puzzles_added < V3_MAX_BONUS_PUZZLES
            && v3->puzzles_solved < v3->total_puzzles - 1U) {
            out->action = NPC_ACTION_ADD_BONUS;
            snprintf(out->phrase_key, sizeof(out->phrase_key),
                     "adaptation.bonus_puzzle_added.%d",
                     (int)(v3->bonus_puzzles_added % 2));
            v3->bonus_puzzles_added++;
            ESP_LOGI(TAG, "add bonus puzzle #%d (pct=%lu)",
                     (int)v3->bonus_puzzles_added, (unsigned long)pct);
            return true;
        }

        // Slow group: skip optional puzzle at 160% threshold
        if (pct > V3_SKIP_SCENE_PCT
            && v3->puzzles_solved < v3->total_puzzles - 1U) {
            out->action = NPC_ACTION_SKIP_PUZZLE;
            snprintf(out->phrase_key, sizeof(out->phrase_key),
                     "adaptation.puzzle_skipped.0");
            ESP_LOGW(TAG, "skip puzzle (pct=%lu)", (unsigned long)pct);
            return true;
        }
    }

    return false;
}
