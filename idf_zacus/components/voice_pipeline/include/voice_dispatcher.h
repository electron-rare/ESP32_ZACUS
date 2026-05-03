// voice_dispatcher — slice 8 routing layer between the voice pipeline
// (STT + LLM intent results) and the npc_engine.
//
// Two entry points (one per server-side message type emitted by the
// MacStudio voice-bridge):
//
//   * voice_dispatcher_handle_stt(text, final)
//       Called from voice_pipeline_ws when the bridge sends
//       `{"type":"stt", ...}`. Slice 8 only acts on `final == true`
//       transcripts. The text is normalized (lowercase + ASCII fold for
//       common French diacritics) and matched against a small keyword
//       set ("indice", "aide", "hint", "bloqué", …). On a hit the
//       dispatcher fires `npc_engine_request_hint()` directly — the
//       hints_client side is already async so this stays non-blocking.
//       On a miss the call is logged and dropped: the LLM intent path
//       is owned server-side (voice-bridge forwards to /voice/intent
//       and pushes the answer back as `{"type":"intent", ...}`).
//
//   * voice_dispatcher_handle_intent(text, model)
//       Called from voice_pipeline_ws when the bridge sends
//       `{"type":"intent", ...}`. Slice 8 only logs the payload and
//       fires a best-effort `intent_ack` cue through npc_engine. Real
//       intent → action mapping (open puzzle, give hint, etc.) lands
//       in a later slice once the scenario engine exposes the active
//       puzzle context.
//
// The dispatcher does NOT own a queue or a task: STT callbacks already
// run on the WebSocket event-loop task and `npc_engine_request_hint`
// is itself async (spawns a worker). Direct invocation keeps the slice
// small; introducing a FreeRTOS queue / dispatcher task is a follow-up
// once we need to coalesce or rate-limit.

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the dispatcher. Must be called after `npc_engine_init()`
// (and ideally after `hints_client_init()` so the request_hint path
// reaches the real backend instead of the local stub). Idempotent.
esp_err_t voice_dispatcher_init(void);

// Handle one STT segment. `text` may be NULL/empty (no-op). `final`
// gates the action: interim transcripts are ignored in slice 8.
void voice_dispatcher_handle_stt(const char *text, bool final);

// Handle one LLM intent payload. Either argument may be NULL.
void voice_dispatcher_handle_intent(const char *text, const char *model);

#ifdef __cplusplus
}
#endif
