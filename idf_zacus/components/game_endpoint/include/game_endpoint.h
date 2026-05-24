// game_endpoint — REST surface for runtime game configuration.
//
// Slice 12 of the IDF migration. Today this exposes a single resource:
//
//   GET  /game/group_profile  — read the active hints group profile.
//   POST /game/group_profile  — set a new profile, persist to NVS, and
//                                push it to the hints_client so the
//                                next /hints/ask body carries it.
//
// The handlers are attached to the existing esp_http_server instance
// owned by the ota_server component (port 80) — same pattern as
// voice_hook_endpoint. No second TCP socket, no second worker pool.
//
// NVS persistence: namespace "zacus", key "group_profile". This is the
// same key main.c reads at boot to seed the hints client, so a
// successful POST survives reboot without any flash step.
//
// Validation is delegated to hints_client_set_group_profile() which
// already enforces the "TECH" / "NON_TECH" / "MIXED" / "BOTH" whitelist.
// On invalid input the NVS write is skipped and the client keeps its
// previous value.

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cap the request body so a malformed PLIP / dashboard client cannot
// blow up the worker stack. 256 bytes is plenty for {"group_profile":
// "NON_TECH"} (~30 bytes) plus future additive fields.
#define GAME_ENDPOINT_MAX_BODY_BYTES 256

// Larger cap for the Runtime 3 IR scenario blob. 64 KiB lets a
// reasonable escape-room scenario (~50 steps, dialogues + actions)
// fit comfortably. Scenarios that exceed this should be split
// across multiple boards or trimmed.
#define GAME_ENDPOINT_MAX_SCENARIO_BYTES (64 * 1024)

// LittleFS partition label declared in partitions.csv. game_endpoint
// mounts lazily on first scenario POST. media_manager may also mount
// the same label — esp_vfs_littlefs_register is idempotent per label.
#define GAME_ENDPOINT_STORAGE_LABEL  "storage"
// main.c mounts the storage partition at /littlefs at boot — we reuse the
// same mount point instead of registering a second base path for the same
// partition (which fails silently with INVALID_STATE).
#define GAME_ENDPOINT_STORAGE_BASE   "/littlefs"
#define GAME_ENDPOINT_SCENARIO_PATH  GAME_ENDPOINT_STORAGE_BASE "/scenario.json"
#define GAME_ENDPOINT_SCENARIO_BAK   GAME_ENDPOINT_STORAGE_BASE "/scenario.bak"

/**
 * @brief Attach all game endpoint handlers to an existing esp_http_server.
 *
 * Registers:
 *   - GET/POST /game/group_profile  (slice 12, runtime hints profile)
 *   - POST /game/scenario           (slice 13, Runtime 3 IR hot-load)
 *
 * Pass the handle returned by `ota_server_get_handle()`. Returns
 * ESP_ERR_INVALID_ARG if `server` is NULL, or any error propagated
 * from `httpd_register_uri_handler()`.
 */
esp_err_t game_endpoint_init(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
