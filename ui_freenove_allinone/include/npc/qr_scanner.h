// qr_scanner.h - QR decode task with ZACUS protocol, debounce, anti-cheat.
// Wraps the existing ui::QrScanController with NPC-level logic.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QR_PREFIX              "ZACUS:"
#define QR_PREFIX_LEN          6
#define QR_SCENE_ID_MAX        32
#define QR_EVENT_ID_MAX        32
#define QR_CHECKSUM_LEN        4
#define QR_DEBOUNCE_MS         30000
#define QR_HMAC_KEY            "zacus-escape-2026"
#define QR_MAX_HISTORY         16

typedef enum {
    QR_VALID = 0,
    QR_INVALID_FORMAT,
    QR_INVALID_CHECKSUM,
    QR_WRONG_SCENE,
    QR_DEBOUNCED,
    QR_ALREADY_SCANNED
} qr_validation_t;

typedef struct {
    char scene_id[QR_SCENE_ID_MAX];
    char event_id[QR_EVENT_ID_MAX];
    char checksum[QR_CHECKSUM_LEN + 1];
    qr_validation_t status;
    uint32_t scanned_at_ms;
} qr_decode_result_t;

/// Initialize QR scanner subsystem.
void qr_npc_init(void);

/// Parse a raw QR payload string into structured result.
/// Does NOT validate scene or checksum yet.
bool qr_npc_parse(const char* payload, qr_decode_result_t* out);

/// Validate a parsed QR result against current game state.
/// current_scene_id: the scene the player is currently in.
/// now_ms: current time for debounce check.
qr_validation_t qr_npc_validate(const qr_decode_result_t* decoded,
                                 const char* current_scene_id,
                                 uint32_t now_ms);

/// Compute HMAC-SHA256 checksum (truncated to 4 hex chars).
/// Input: "SCENE_ID:EVENT_ID", output: 4 hex chars in out_checksum.
void qr_npc_compute_checksum(const char* scene_id, const char* event_id,
                              char out_checksum[5]);

/// Record a scan in history (for anti-cheat / dedup).
void qr_npc_record_scan(const char* scene_id, const char* event_id, uint32_t now_ms);

/// Check if a specific QR was already scanned.
bool qr_npc_was_scanned(const char* scene_id, const char* event_id);

/// Get total valid scans count.
uint8_t qr_npc_scan_count(void);

#ifdef __cplusplus
}
#endif
