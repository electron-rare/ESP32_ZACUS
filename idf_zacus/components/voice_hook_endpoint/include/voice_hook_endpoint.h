// voice_hook_endpoint — REST bridge between the PLIP retro-telephone
// (Si3210 SLIC hook switch) and the Zacus master voice pipeline.
//
// Slice 10 deliverable: PLIP detects off-hook / on-hook on the Si3210
// INT line and POSTs the new state to the Zacus master at
// `POST /voice/hook`. Off-hook bypasses the wake-word and arms the
// voice pipeline directly (LISTENING + capture). On-hook tears the
// streaming WebSocket down and returns the pipeline to IDLE.
//
// The handlers are attached to the existing esp_http_server instance
// owned by the ota_server component (port 80) — no second httpd
// listener, no second TCP socket. Caller is expected to obtain the
// handle from `ota_server_get_handle()` after a successful
// `ota_server_init()`.
//
// Routes registered:
//   POST /voice/hook         — body {"state":"off"|"on","reason"?:"..."}
//   GET  /voice/hook/state   — debug introspection (current voice state)
//
// Thread safety: the esp_http_server runs each handler on its own
// worker task. The voice_pipeline_* APIs invoked here are documented
// as safe to call from any task (the pipeline serialises state
// transitions internally via its capture-task mailbox). Off-hook
// requests are idempotent — bouncing the hook switch is safe.

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wire-protocol constants — also used by the PLIP firmware as the only
// two valid `state` payload values.
#define VOICE_HOOK_OFF "off"   // pickup    → arm voice pipeline
#define VOICE_HOOK_ON  "on"    // hangup    → tear pipeline down

// Maximum body size accepted by POST /voice/hook. Anything larger is
// rejected with 413 to keep the worker-stack footprint bounded.
#define VOICE_HOOK_MAX_BODY_BYTES 256

/**
 * @brief Attach `/voice/hook` + `/voice/hook/state` handlers to an
 *        existing esp_http_server.
 *
 * Pass the handle returned by `ota_server_get_handle()`. Returns
 * ESP_ERR_INVALID_ARG if `server` is NULL, or any error propagated
 * from `httpd_register_uri_handler()`.
 */
esp_err_t voice_hook_endpoint_init(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
