#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Version info (override in each puzzle's CMakeLists.txt) ─────────────────
#ifndef OTA_FIRMWARE_NAME
#define OTA_FIRMWARE_NAME "zacus_puzzle"
#endif

#ifndef OTA_FIRMWARE_VERSION
#define OTA_FIRMWARE_VERSION "1.0.0"
#endif

// ─── Configuration ────────────────────────────────────────────────────────────
#define OTA_SERVER_PORT         80
#define OTA_RATE_LIMIT_SECS     60        // Minimum seconds between OTA updates
#define OTA_WATCHDOG_SECS       30        // Auto-rollback if new firmware crashes within this
#define OTA_MAX_UPLOAD_SIZE     (4 * 1024 * 1024)  // 4 MB max firmware size
#define OTA_CHUNK_SIZE          4096

// ─── State ────────────────────────────────────────────────────────────────────
typedef enum {
    OTA_STATE_IDLE        = 0,
    OTA_STATE_DOWNLOADING = 1,
    OTA_STATE_VERIFYING   = 2,
    OTA_STATE_REBOOTING   = 3,
    OTA_STATE_ERROR       = 4,
} ota_state_t;

typedef struct {
    ota_state_t state;
    int         progress;       // 0-100
    char        error[128];
    uint32_t    bytes_received;
    uint32_t    total_bytes;
    int64_t     last_ota_time;  // Unix timestamp of last OTA attempt
} ota_status_t;

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * @brief Initialize the OTA HTTP server on port 80.
 *
 * Registers 5 endpoints:
 *   GET  /version      -> firmware name, version, IDF version
 *   GET  /status       -> battery, heap, uptime, ESP-NOW peers
 *   POST /ota          -> receive .bin, write to OTA partition, reboot
 *   GET  /ota/status   -> current OTA state and progress
 *   POST /ota/rollback -> revert to previous firmware partition
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ota_server_init(void);

/**
 * @brief Mark current firmware as valid (call after successful startup).
 *
 * Cancels the rollback watchdog. Call this after all subsystems have
 * initialized successfully, typically 5-10 seconds after boot.
 */
void ota_server_mark_valid(void);

/**
 * @brief Get the current OTA status.
 */
const ota_status_t* ota_server_get_status(void);

/**
 * @brief Register a callback invoked when an OTA update completes.
 *
 * Called before the device reboots. Use to flush pending data to NVS.
 */
void ota_server_set_complete_cb(void (*cb)(bool success));

#ifdef __cplusplus
}
#endif
