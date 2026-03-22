#include "analytics/game_analytics.h"
#include <cstring>

namespace zacus {
namespace analytics {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SessionStats s_session;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Find a puzzle entry by id, or nullptr if not found.
static PuzzleStats* findPuzzle(const char* puzzle_id) {
    for (uint8_t i = 0; i < s_session.puzzle_count; ++i) {
        if (strncmp(s_session.puzzles[i].puzzle_id, puzzle_id, sizeof(PuzzleStats::puzzle_id)) == 0) {
            return &s_session.puzzles[i];
        }
    }
    return nullptr;
}

/// Generate a hex session id from EFuse MAC + millis.
static void generateSessionId(char* buf, size_t len) {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t ms  = millis();
    snprintf(buf, len, "%08X%08X",
             (uint32_t)(mac ^ ((uint64_t)ms << 16)),
             (uint32_t)((mac >> 32) ^ ms));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void analyticsInit() {
    memset(&s_session, 0, sizeof(s_session));
}

void analyticsStartSession(const char* session_id) {
    memset(&s_session, 0, sizeof(s_session));
    if (session_id && session_id[0] != '\0') {
        strncpy(s_session.session_id, session_id, sizeof(s_session.session_id) - 1);
        s_session.session_id[sizeof(s_session.session_id) - 1] = '\0';
    } else {
        generateSessionId(s_session.session_id, sizeof(s_session.session_id));
    }
    s_session.start_ms = millis();
}

void analyticsPuzzleStart(const char* puzzle_id) {
    if (!puzzle_id) return;
    // Don't duplicate
    if (findPuzzle(puzzle_id)) return;
    if (s_session.puzzle_count >= 8) return; // array full

    PuzzleStats& p = s_session.puzzles[s_session.puzzle_count];
    memset(&p, 0, sizeof(PuzzleStats));
    strncpy(p.puzzle_id, puzzle_id, sizeof(p.puzzle_id) - 1);
    p.puzzle_id[sizeof(p.puzzle_id) - 1] = '\0';
    p.start_ms  = millis();
    p.solve_ms  = 0;
    p.attempts  = 0;
    p.hints_requested = 0;
    p.solved    = false;
    s_session.puzzle_count++;
}

void analyticsPuzzleAttempt(const char* puzzle_id) {
    PuzzleStats* p = findPuzzle(puzzle_id);
    if (!p) return;
    if (p->attempts < 255) p->attempts++;
    if (s_session.total_attempts < 255) s_session.total_attempts++;
}

void analyticsHintRequested(const char* puzzle_id) {
    PuzzleStats* p = findPuzzle(puzzle_id);
    if (!p) return;
    if (p->hints_requested < 255) p->hints_requested++;
    if (s_session.total_hints < 255) s_session.total_hints++;
}

void analyticsPuzzleSolved(const char* puzzle_id) {
    PuzzleStats* p = findPuzzle(puzzle_id);
    if (!p || p->solved) return;
    p->solved   = true;
    p->solve_ms = millis() - p->start_ms;
    if (s_session.puzzles_solved < 255) s_session.puzzles_solved++;
}

void analyticsToJson(JsonDocument& doc) {
    uint32_t now = millis();
    s_session.total_duration_ms = now - s_session.start_ms;

    doc["session_id"]      = s_session.session_id;
    doc["duration_ms"]     = s_session.total_duration_ms;
    doc["puzzles_solved"]  = s_session.puzzles_solved;
    doc["total_hints"]     = s_session.total_hints;
    doc["total_attempts"]  = s_session.total_attempts;

    JsonArray arr = doc["puzzles"].to<JsonArray>();
    for (uint8_t i = 0; i < s_session.puzzle_count; ++i) {
        const PuzzleStats& p = s_session.puzzles[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["puzzle_id"]   = p.puzzle_id;
        obj["solved"]      = p.solved;
        obj["attempts"]    = p.attempts;
        obj["hints"]       = p.hints_requested;
        obj["duration_ms"] = p.solved ? p.solve_ms : (now - p.start_ms);
    }
}

const SessionStats& analyticsGetSession() {
    uint32_t now = millis();
    s_session.total_duration_ms = now - s_session.start_ms;
    return s_session;
}

void analyticsReset() {
    memset(&s_session, 0, sizeof(s_session));
}

} // namespace analytics
} // namespace zacus
