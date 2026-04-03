// game_coordinator.cpp — Top-level V3 game state machine.
// Integrates: npc_engine, npc_v3, espnow_master, tts_client.
// State machine: IDLE → INTRO → PROFILING → ADAPTIVE → CLIMAX → OUTRO
#include "npc/game_coordinator.h"
#include "npc/npc_engine.h"
#include "npc/npc_v3.h"
#include "npc/espnow_master.h"
#include "npc/tts_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "GAME_COORD";

// ============================================================
// Phase definitions
// ============================================================

typedef enum {
    GAME_IDLE      = 0,
    GAME_INTRO     = 1,
    GAME_PROFILING = 2,
    GAME_ADAPTIVE  = 3,
    GAME_CLIMAX    = 4,
    GAME_OUTRO     = 5,
} game_phase_t;

// ============================================================
// Game state
// ============================================================

typedef struct {
    game_phase_t   phase;
    npc_state_t    npc;
    npc_v3_state_t v3;
    uint8_t        current_puzzle_idx;         // index into parcours[]
    uint8_t        parcours[NPC_V3_MAX_PARCOURS];
    uint8_t        parcours_len;
    char           final_code[9];              // 8-digit assembled code + null
    uint32_t       score;
    uint32_t       hints_given_total;
} game_state_t;

static game_state_t s_game;

// ============================================================
// TTS helpers
// ============================================================

static void play_phrase(const char* key) {
    // In production: look up key in phrase bank → get French text → tts_speak_v3()
    // For now: log the key; actual TTS dispatch via tts_speak_v3 when text available.
    ESP_LOGI(TAG, "NPC phrase: [%s]", key);
}

// ============================================================
// Difficulty selection per group
// ============================================================

static puzzle_difficulty_t difficulty_for_profile(group_profile_t profile) {
    if (profile == GROUP_NON_TECH) return DIFFICULTY_EASY;
    if (profile == GROUP_TECH)     return DIFFICULTY_HARD;
    return DIFFICULTY_NORMAL;
}

// ============================================================
// Puzzle expected durations by id (seconds → ms)
// ============================================================

static uint32_t expected_duration_for_puzzle(uint8_t puzzle_id) {
    switch (puzzle_id) {
        case 1: return 300000U;   // P1_SON: 5 min
        case 2: return 360000U;   // P2_CIRCUIT: 6 min
        case 3: return 240000U;   // P3_QR: 4 min
        case 4: return 300000U;   // P4_RADIO: 5 min
        case 5: return 420000U;   // P5_MORSE: 7 min
        case 6: return 360000U;   // P6_SYMBOLES: 6 min
        case 7: return 300000U;   // P7_COFFRE: 5 min
        default: return 300000U;
    }
}

// ============================================================
// Phase transitions
// ============================================================

static void transition_to_phase(game_phase_t phase, uint32_t now_ms) {
    ESP_LOGI(TAG, "phase %d → %d", (int)s_game.phase, (int)phase);
    s_game.phase = phase;

    switch (phase) {
    case GAME_INTRO:
        play_phrase("ambiance.intro.0");
        // Intro monologue plays for ~5 min; game_coordinator_tick()
        // triggers PROFILING automatically based on timer.
        break;

    case GAME_PROFILING:
        play_phrase("profiling.start");
        espnow_master_reset_puzzle(1);   // P1_SON
        espnow_master_reset_puzzle(2);   // P2_CIRCUIT
        npc_on_scene_change(&s_game.npc, 1,
                            expected_duration_for_puzzle(1), now_ms);
        break;

    case GAME_ADAPTIVE: {
        // Classify group and select parcours
        group_profile_t profile = npc_v3_classify_group(&s_game.v3);
        s_game.v3.group_profile = profile;
        s_game.parcours_len = npc_v3_select_puzzles(profile, s_game.parcours);
        s_game.v3.total_puzzles = s_game.parcours_len;
        s_game.current_puzzle_idx = 0;

        // Announce group profile
        if (profile == GROUP_TECH) {
            play_phrase("adaptation.group_tech_detected.0");
        } else if (profile == GROUP_NON_TECH) {
            play_phrase("adaptation.group_non_tech_detected.0");
        } else {
            play_phrase("adaptation.group_mixed_detected.0");
        }

        // Activate first adaptive puzzle
        uint8_t first_puzzle = s_game.parcours[0];
        espnow_master_reset_puzzle(first_puzzle);
        espnow_master_configure_puzzle(first_puzzle,
                                       difficulty_for_profile(profile));
        npc_on_scene_change(&s_game.npc, first_puzzle,
                            expected_duration_for_puzzle(first_puzzle), now_ms);
        ESP_LOGI(TAG, "ADAPTIVE: profile=%d parcours_len=%d first_puzzle=P%d",
                 (int)profile, (int)s_game.parcours_len, (int)first_puzzle);
        break;
    }

    case GAME_CLIMAX:
        // Assemble 8-digit final code from all puzzle code fragments
        espnow_master_assemble_code(s_game.final_code);
        ESP_LOGI(TAG, "final code: %s", s_game.final_code);
        espnow_master_send_final_code(s_game.final_code);
        espnow_master_reset_puzzle(7);   // P7_COFFRE
        play_phrase("ambiance.climax");
        npc_on_scene_change(&s_game.npc, 7,
                            expected_duration_for_puzzle(7), now_ms);
        break;

    case GAME_OUTRO: {
        bool success = (s_game.final_code[0] != 0 && s_game.hints_given_total < 5);
        if (success) {
            play_phrase("ambiance.outro_success.0");
        } else {
            play_phrase("ambiance.outro_partial.0");
        }
        // Compute score: base - hint penalties
        s_game.score = 1000U;
        if (s_game.hints_given_total > 0) {
            uint32_t penalty = s_game.hints_given_total * 50U;
            s_game.score = (s_game.score > penalty) ? (s_game.score - penalty) : 0U;
        }
        ESP_LOGI(TAG, "OUTRO: score=%lu hints=%lu",
                 (unsigned long)s_game.score,
                 (unsigned long)s_game.hints_given_total);
        break;
    }

    default:
        break;
    }
}

// ============================================================
// ESP-NOW event callback (called from ISR-safe queue)
// ============================================================

static void on_espnow_event(const espnow_event_t* ev) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint8_t puzzle_id = ev->puzzle_id;

    if (ev->type == ESPNOW_EV_PUZZLE_SOLVED) {
        ESP_LOGI(TAG, "P%d SOLVED (phase=%d)", (int)puzzle_id, (int)s_game.phase);

        if (s_game.phase == GAME_PROFILING) {
            // Record profiling time and check if both P1+P2 done
            npc_v3_on_profiling_solved(&s_game.v3, puzzle_id, ev->solve_time_ms);
            if (s_game.v3.profiling_p1_time_ms > 0
                && s_game.v3.profiling_p2_time_ms > 0) {
                transition_to_phase(GAME_ADAPTIVE, now);
            }

        } else if (s_game.phase == GAME_ADAPTIVE) {
            npc_v3_on_puzzle_solved(&s_game.v3, puzzle_id);
            s_game.current_puzzle_idx++;

            // Check if all adaptive puzzles complete (last entry is P7_COFFRE)
            bool all_done = (s_game.current_puzzle_idx >= s_game.parcours_len - 1U);
            if (all_done) {
                transition_to_phase(GAME_CLIMAX, now);
            } else {
                // Activate next puzzle
                uint8_t next = s_game.parcours[s_game.current_puzzle_idx];
                espnow_master_reset_puzzle(next);
                espnow_master_configure_puzzle(next,
                    difficulty_for_profile(s_game.v3.group_profile));
                npc_on_scene_change(&s_game.npc, next,
                                    expected_duration_for_puzzle(next), now);
                ESP_LOGI(TAG, "next puzzle: P%d", (int)next);
            }

        } else if (s_game.phase == GAME_CLIMAX && puzzle_id == 7) {
            transition_to_phase(GAME_OUTRO, now);
        }

    } else if (ev->type == ESPNOW_EV_HINT_REQUEST) {
        s_game.hints_given_total++;
        npc_on_hint_request(&s_game.npc, now);
        // Dispatch contextual hint via NPC engine
        npc_decision_t dec;
        if (npc_evaluate(&s_game.npc, now, &dec)) {
            if (dec.audio_source == NPC_AUDIO_LIVE_TTS) {
                // tts_speak_v3(dec.phrase_text, npc_build_sd_key(...))
                ESP_LOGI(TAG, "hint TTS: %s", dec.phrase_text);
            } else if (dec.audio_source == NPC_AUDIO_SD_CONTEXTUAL
                       || dec.audio_source == NPC_AUDIO_SD_GENERIC) {
                // Play MP3 from SD: dec.sd_path
                ESP_LOGI(TAG, "hint SD: %s", dec.sd_path);
            }
        }
    }
}

// ============================================================
// Public API
// ============================================================

void game_coordinator_init(uint32_t target_duration_ms) {
    memset(&s_game, 0, sizeof(s_game));
    npc_init(&s_game.npc);
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    npc_v3_init(&s_game.v3, target_duration_ms, now);
    espnow_master_init(on_espnow_event);
    s_game.phase = GAME_IDLE;
    ESP_LOGI(TAG, "init complete, target=%lu ms", (unsigned long)target_duration_ms);
}

void game_coordinator_start(void) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    transition_to_phase(GAME_INTRO, now);
}

void game_coordinator_tick(void) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // Process all pending ESP-NOW messages
    espnow_master_process();

    // Periodic NPC mood update
    npc_update_mood(&s_game.npc, now);
    tts_health_tick(now);

    // Auto-advance INTRO → PROFILING after 5-minute intro sequence
    if (s_game.phase == GAME_INTRO) {
        static const uint32_t kIntroDurationMs = 5U * 60U * 1000U;
        uint32_t intro_elapsed = now - s_game.v3.game_start_ms;
        if (intro_elapsed >= kIntroDurationMs) {
            transition_to_phase(GAME_PROFILING, now);
        }
        return;
    }

    // V3 adaptive decisions (profiling, pacing, duration warning)
    npc_v3_decision_t v3_dec;
    if (npc_v3_evaluate(&s_game.v3, &s_game.npc, now, &v3_dec)) {
        switch (v3_dec.action) {
        case NPC_ACTION_SET_PROFILE:
            // Profile was set inside npc_v3_evaluate; trigger adaptive transition
            if (s_game.phase == GAME_PROFILING) {
                transition_to_phase(GAME_ADAPTIVE, now);
            }
            break;

        case NPC_ACTION_SKIP_PUZZLE:
            if (s_game.phase == GAME_ADAPTIVE
                && s_game.current_puzzle_idx < s_game.parcours_len - 1U) {
                s_game.current_puzzle_idx++;
                uint8_t next = s_game.parcours[s_game.current_puzzle_idx];
                ESP_LOGW(TAG, "skip → P%d", (int)next);
                espnow_master_reset_puzzle(next);
                espnow_master_configure_puzzle(next,
                    difficulty_for_profile(s_game.v3.group_profile));
                npc_on_scene_change(&s_game.npc, next,
                                    expected_duration_for_puzzle(next), now);
            }
            break;

        case NPC_ACTION_DURATION_WARN:
            // duration_warning_sent already set inside npc_v3_check_duration
            break;

        case NPC_ACTION_ADD_BONUS:
            // bonus_puzzles_added already incremented inside npc_v3_evaluate
            // game_coordinator would insert a bonus puzzle into parcours here
            ESP_LOGI(TAG, "bonus puzzle signalled; integration pending");
            break;

        default:
            break;
        }

        if (v3_dec.phrase_key[0]) {
            play_phrase(v3_dec.phrase_key);
        }
    }

    // Base NPC evaluation (stuck timer, QR reaction, etc.)
    npc_decision_t dec;
    if (npc_evaluate(&s_game.npc, now, &dec)) {
        if (dec.audio_source == NPC_AUDIO_LIVE_TTS) {
            ESP_LOGI(TAG, "base NPC TTS: %s", dec.phrase_text);
        } else if (dec.audio_source != NPC_AUDIO_NONE) {
            ESP_LOGI(TAG, "base NPC SD: %s", dec.sd_path);
        }
    }
}

game_phase_t game_coordinator_phase(void) {
    return (game_phase_t)s_game.phase;
}

uint32_t game_coordinator_score(void) {
    return s_game.score;
}
