// npc_v3.h — V3 Adaptive NPC extensions for Professor Zacus.
// Group profiling, adaptive parcours, duration targeting.
// Extends npc_engine.h — include after it.
#pragma once

#include "npc/npc_engine.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Group profile (determined after Phase 2 profiling puzzles)
// ============================================================

typedef enum {
    GROUP_UNKNOWN   = 0,
    GROUP_TECH      = 1,   // P2 fast, P1 normal/slow
    GROUP_NON_TECH  = 2,   // P1 fast, P2 normal/slow
    GROUP_MIXED     = 3,   // both similar speed
} group_profile_t;

// ============================================================
// V3 adaptive NPC state (companion to npc_state_t)
// ============================================================

typedef struct {
    group_profile_t group_profile;
    uint32_t        target_duration_ms;     // game master configured (30–90 min)
    uint32_t        game_start_ms;
    uint32_t        profiling_p1_time_ms;   // P1 solve time (0 = not yet solved)
    uint32_t        profiling_p2_time_ms;   // P2 solve time (0 = not yet solved)
    uint8_t         puzzles_solved;
    uint8_t         total_puzzles;
    uint8_t         bonus_puzzles_added;
    bool            duration_warning_sent;
} npc_v3_state_t;

// ============================================================
// V3 adaptive actions
// ============================================================

typedef enum {
    NPC_ACTION_NONE           = 0,
    NPC_ACTION_ADD_BONUS      = 1,   // add bonus puzzle for fast group
    NPC_ACTION_SKIP_PUZZLE    = 2,   // skip optional puzzle for slow group
    NPC_ACTION_PROACTIVE_HINT = 3,   // unsolicited hint for stuck group
    NPC_ACTION_DURATION_WARN  = 4,   // 80% time consumed warning
    NPC_ACTION_SET_PROFILE    = 5,   // classify group after profiling
} npc_v3_action_t;

typedef struct {
    npc_v3_action_t  action;
    group_profile_t  new_profile;    // valid for NPC_ACTION_SET_PROFILE
    uint8_t          puzzle_to_skip; // valid for NPC_ACTION_SKIP_PUZZLE (puzzle id 1-7)
    char             phrase_key[64]; // npc_phrases.yaml key to play
} npc_v3_decision_t;

// ============================================================
// Puzzle selection helpers
// ============================================================

// Maximum parcours length (MIXED has 6 puzzles)
#define NPC_V3_MAX_PARCOURS  8

// Populate parcours array based on group profile.
// out_puzzles: array of at least NPC_V3_MAX_PARCOURS uint8_t.
// Returns length of populated parcours.
uint8_t npc_v3_select_puzzles(group_profile_t profile,
                               uint8_t out_puzzles[NPC_V3_MAX_PARCOURS]);

// ============================================================
// V3 NPC API
// ============================================================

/// Initialize V3 state.
/// target_duration_ms: configured by game master at boot (1800000 = 30 min, max 5400000).
/// now_ms: current tick count in milliseconds.
void npc_v3_init(npc_v3_state_t* v3, uint32_t target_duration_ms, uint32_t now_ms);

/// Record profiling puzzle solve time.
/// puzzle_id: 1 (P1_SON) or 2 (P2_CIRCUIT).
/// solve_time_ms: elapsed ms from puzzle activation to solve.
void npc_v3_on_profiling_solved(npc_v3_state_t* v3, uint8_t puzzle_id,
                                 uint32_t solve_time_ms);

/// Classify group profile from P1/P2 solve times.
/// Returns GROUP_UNKNOWN if both profiling times are not yet recorded.
group_profile_t npc_v3_classify_group(const npc_v3_state_t* v3);

/// Evaluate V3 adaptive rules; produces at most one action per call.
/// Returns true if an action should be executed (out is populated).
/// Call from game_coordinator_tick() after npc_evaluate().
bool npc_v3_evaluate(npc_v3_state_t* v3, const npc_state_t* base,
                     uint32_t now_ms, npc_v3_decision_t* out);

/// Check duration budget: returns true and populates out if 80% of
/// target_duration_ms has elapsed with puzzles still remaining.
/// Marks duration_warning_sent to prevent repeats.
bool npc_v3_check_duration(npc_v3_state_t* v3, uint32_t now_ms,
                            npc_v3_decision_t* out);

/// Notify V3 engine that an adaptive-phase puzzle was solved.
/// Updates puzzles_solved counter and bonus_puzzles_added if applicable.
void npc_v3_on_puzzle_solved(npc_v3_state_t* v3, uint8_t puzzle_id);

#ifdef __cplusplus
}
#endif
