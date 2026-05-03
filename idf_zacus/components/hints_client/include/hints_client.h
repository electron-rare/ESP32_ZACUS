// hints_client — HTTP client to the Zacus hints engine.
//
// POSTs JSON {"puzzle_id":..,"level":..,"session_id":..,"group_profile":..}
// to {base_url}/hints/ask and parses the "hint" / "source" / "refused" /
// "cooldown_until_ms" fields.
//
// Slice 11 (P5) adds two best-effort lifecycle endpoints used by the
// scenario engine to give the hints backend richer context for its
// adaptive policy:
//   * /hints/puzzle_start    — entered a new pivot
//   * /hints/attempt_failed  — operator reported an invalid input
// Both POST a minimal `{session_id, puzzle_id}` body. Failures are logged
// but never fatal: the engine works without these signals, they only
// improve the hint quality.
//
// Surfaces:
//   * hints_client_ask()           — synchronous, blocks the calling task up
//                                     to HINTS_CLIENT_TIMEOUT_MS. Returns
//                                     ESP_OK on success and writes the hint
//                                     into out_hint.
//   * hints_client_ask_async()     — spawns a one-shot worker task that
//                                     performs the request and invokes `cb`
//                                     when done.
//   * hints_client_puzzle_start()  — synchronous best-effort POST.
//   * hints_client_attempt_failed()— synchronous best-effort POST.
//   * hints_client_set_group_profile() — global profile attached to every
//                                     /hints/ask payload (default "MIXED").

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HINTS_CLIENT_BASE_URL_MAX   128
#define HINTS_CLIENT_HINT_MAX       512
#define HINTS_CLIENT_TIMEOUT_MS     10000  // 10 s — covers MLX 32B latency
#define HINTS_CLIENT_LIFECYCLE_TIMEOUT_MS  5000  // /puzzle_start + /attempt_failed
#define HINTS_CLIENT_GROUP_PROFILE_MAX     32   // "TECH","NON_TECH","MIXED","BOTH"

// Callback signature mirrors npc_engine.h's npc_hint_callback_t so the
// engine can forward `cb` directly without a trampoline.
typedef void (*hints_client_callback_t)(uint8_t puzzle_id, uint8_t level,
                                        esp_err_t status, const char *text,
                                        void *user_ctx);

// Initialise the client with the base URL of the hints engine
// (e.g. "http://192.168.0.150:8302"). Caller retains ownership of the
// string — it is copied internally.
esp_err_t hints_client_init(const char *base_url);

// Returns true once hints_client_init() succeeded.
bool hints_client_is_ready(void);

// Synchronous request. `out_hint` receives a NUL-terminated UTF-8 string,
// up to `out_size` bytes. Errors:
//   ESP_ERR_INVALID_STATE   not initialised
//   ESP_ERR_INVALID_ARG     bad params
//   ESP_ERR_TIMEOUT         server did not respond in time
//   ESP_FAIL                HTTP non-2xx or JSON parse error
esp_err_t hints_client_ask(const char *puzzle_id, uint8_t level,
                           char *out_hint, size_t out_size);

// Async wrapper. Spawns a worker FreeRTOS task with `stack` bytes of stack
// (default 6144 if 0 passed) and priority `prio` (default 5 if 0). The worker
// calls hints_client_ask() and then `cb` from its own context.
//
// `puzzle_id_str` and the user_ctx pointer are copied into the worker arg
// block, so the caller does not need to keep them alive.
esp_err_t hints_client_ask_async(const char *puzzle_id_str,
                                 uint8_t puzzle_id_num,
                                 uint8_t level,
                                 hints_client_callback_t cb,
                                 void *user_ctx,
                                 uint32_t stack,
                                 uint8_t prio);

// Slice 11 (P5): notify the hints engine that the operator just entered
// the pivot identified by `puzzle_id`. Synchronous POST, timeout
// HINTS_CLIENT_LIFECYCLE_TIMEOUT_MS. Returns ESP_OK on any 2xx
// (the engine treats this as idempotent), ESP_FAIL otherwise. The call
// is best-effort — callers should log failures and continue.
esp_err_t hints_client_puzzle_start(const char *puzzle_id);

// Same shape as hints_client_puzzle_start, but for the
// /hints/attempt_failed endpoint. Bumps the engine's failure counter
// for the current pivot, which feeds the adaptive escalation policy.
esp_err_t hints_client_attempt_failed(const char *puzzle_id);

// Replace the global group profile attached to every /hints/ask body.
// `profile` must be one of "TECH", "NON_TECH", "MIXED", "BOTH". Any
// other value (including NULL/empty) is rejected with
// ESP_ERR_INVALID_ARG and the previous profile is preserved. Default
// after init() is "MIXED".
esp_err_t hints_client_set_group_profile(const char *profile);

// Returns the currently configured group profile (always non-NULL).
// The pointer is owned by the client; copy if you need it past the
// next set_group_profile() call.
const char *hints_client_group_profile(void);

#ifdef __cplusplus
}
#endif
