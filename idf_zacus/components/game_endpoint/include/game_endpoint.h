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

/**
 * @brief Attach `/game/group_profile` (GET + POST) handlers to an
 *        existing esp_http_server.
 *
 * Pass the handle returned by `ota_server_get_handle()`. Returns
 * ESP_ERR_INVALID_ARG if `server` is NULL, or any error propagated
 * from `httpd_register_uri_handler()`.
 */
esp_err_t game_endpoint_init(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
