// game_coordinator.h — V3 game state machine public API.
// Init → Start → Tick loop. Integrates NPC V3 + ESP-NOW + TTS.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize game coordinator with target game duration.
/// target_duration_ms: 1800000 (30 min) to 5400000 (90 min).
/// Call once before game_coordinator_start().
void game_coordinator_init(uint32_t target_duration_ms);

/// Start the game (transitions to INTRO phase).
void game_coordinator_start(void);

/// Call every ~100ms from main loop or FreeRTOS task.
/// Drives ESP-NOW processing, NPC evaluation, and adaptive logic.
void game_coordinator_tick(void);

/// Return current game phase (cast to game_phase_t from game_coordinator.cpp).
int game_coordinator_phase(void);

/// Return final score (valid after OUTRO phase).
uint32_t game_coordinator_score(void);

#ifdef __cplusplus
}
#endif
