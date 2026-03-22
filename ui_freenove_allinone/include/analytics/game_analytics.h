#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace zacus {
namespace analytics {

struct PuzzleStats {
    char puzzle_id[32];
    uint32_t start_ms;
    uint32_t solve_ms;       // 0 if not solved
    uint8_t attempts;
    uint8_t hints_requested;
    bool solved;
};

struct SessionStats {
    char session_id[32];
    uint32_t start_ms;
    uint32_t total_duration_ms;
    uint8_t puzzles_solved;
    uint8_t total_hints;
    uint8_t total_attempts;
    PuzzleStats puzzles[8];  // max 8 puzzles
    uint8_t puzzle_count;
};

/// Initialize analytics (call from setup)
void analyticsInit();

/// Start a new game session
void analyticsStartSession(const char* session_id = nullptr);

/// Record puzzle start
void analyticsPuzzleStart(const char* puzzle_id);

/// Record puzzle attempt
void analyticsPuzzleAttempt(const char* puzzle_id);

/// Record hint requested
void analyticsHintRequested(const char* puzzle_id);

/// Record puzzle solved
void analyticsPuzzleSolved(const char* puzzle_id);

/// Get current session stats as JSON
void analyticsToJson(JsonDocument& doc);

/// Get current session stats
const SessionStats& analyticsGetSession();

/// Reset session
void analyticsReset();

} // namespace analytics
} // namespace zacus
