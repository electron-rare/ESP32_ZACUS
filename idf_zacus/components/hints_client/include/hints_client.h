// hints_client — HTTP client to the Zacus hints engine.
//
// POSTs JSON {"puzzle_id":..,"level":..,"session_id":..} to {base_url}/hints/ask
// and parses the "hint" / "source" / "refused" / "cooldown_until_ms" fields.
//
// Two surfaces:
//   * hints_client_ask()        — synchronous, blocks the calling task up to
//                                  HINTS_CLIENT_TIMEOUT_MS. Returns ESP_OK on
//                                  success and writes the hint into out_hint.
//   * hints_client_ask_async()  — spawns a one-shot worker task that performs
//                                  the request and invokes `cb` when done. The
//                                  callback signature matches npc_hint_callback_t.

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

#ifdef __cplusplus
}
#endif
