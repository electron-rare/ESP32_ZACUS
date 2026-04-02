// npc_integration.cpp - Wiring layer: NPC engine + TTS + QR + Audio Kit.
//
// This file owns the single global npc_state_t and routes events from the
// main loop into the NPC engine, then dispatches NPC decisions to either:
//   - Live TTS  → HTTP POST to Piper on Tower via audio_kit_play_tts()
//   - SD card   → audio_kit_play_sd() (tower down fallback)
//
// Call order from main.cpp:
//   setup()            → npc_integration_init(audio_kit_base_url)
//   loop tick (5 s)    → npc_integration_tick(now_ms)
//   scene change       → npc_integration_on_scene_change(scene, step)
//   QR detected        → npc_integration_on_qr(payload)
//   phone hook edge    → npc_integration_on_phone_hook(off_hook)
//   game reset         → npc_integration_reset()

#include "npc/npc_integration.h"
#include "npc/npc_engine.h"
#include "npc/tts_client.h"
#include "npc/audio_kit_client.h"
#include "npc/qr_scanner.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

// Piper TTS on Tower — compile-time default matches tts_client.h defines.
static constexpr const char* kTtsPiperUrl = "http://" TTS_TOWER_IP ":8001";
static constexpr const char* kTtsVoice    = TTS_VOICE;

// Default scene duration fed to NPC engine when a scene provides no override.
static constexpr uint32_t kDefaultSceneDurationMs = 3UL * 60UL * 1000UL;

// NPC tick interval: evaluate every 5 seconds (not every loop iteration).
static constexpr uint32_t kNpcTickIntervalMs = 5000U;

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------

static npc_state_t s_npc;
static uint32_t    s_last_tick_ms = 0U;
static bool        s_initialised  = false;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void dispatch_decision(const npc_decision_t* decision);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void npc_integration_init(const char* audio_kit_base_url)
{
    npc_init(&s_npc);
    tts_init();
    audio_kit_client_init(audio_kit_base_url);

    s_last_tick_ms = 0U;
    s_initialised  = true;
}

void npc_integration_tick(uint32_t now_ms)
{
    if (!s_initialised) return;

    // Keep Tower reachability fresh (rate-limited inside tts_health_tick).
    tts_health_tick(now_ms);
    npc_on_tower_status(&s_npc, tts_is_tower_reachable());

    if (now_ms - s_last_tick_ms < kNpcTickIntervalMs) return;
    s_last_tick_ms = now_ms;

    npc_update_mood(&s_npc, now_ms);

    npc_decision_t decision;
    if (npc_evaluate(&s_npc, now_ms, &decision)) {
        dispatch_decision(&decision);
    }
}

void npc_integration_on_scene_change(uint8_t scene, uint8_t step)
{
    if (!s_initialised) return;

    // step parameter reserved for future per-step NPC logic.
    (void)step;

    // Refresh Tower status before wiring the scene change so that the first
    // decision on a new scene already has correct audio routing.
    npc_on_tower_status(&s_npc, tts_is_tower_reachable());
    npc_on_scene_change(&s_npc, scene, kDefaultSceneDurationMs,
                        /* now_ms — caller sets scene_start_ms; pass 0 as
                           a sentinel; the main loop will supply real millis
                           on the next tick.  Scene 0 start time will be
                           corrected on the first npc_integration_tick(). */
                        0U);
}

void npc_integration_on_qr(const char* payload)
{
    if (!s_initialised) return;

    // A non-NULL, non-empty payload means the camera decoded something;
    // pass it through qr_npc_parse to determine validity.
    bool valid = false;
    if (payload != nullptr && payload[0] != '\0') {
        qr_decode_result_t decoded = {};
        if (qr_npc_parse(payload, &decoded)) {
            valid = (decoded.status == QR_VALID);
        }
    }

    // Feed the result into the NPC engine.
    // Use a fixed timestamp of 0 here; the engine only uses now_ms to update
    // last_qr_scan_ms for the debounce window. The main tick provides the
    // authoritative millis(). Integration tests can override via direct state.
    npc_on_qr_scan(&s_npc, valid, /* now_ms */ 0U);

    // If the scan was valid, issue an immediate congratulation decision.
    if (valid) {
        npc_decision_t congratulation = {};
        congratulation.trigger      = NPC_TRIGGER_QR_SCANNED;
        congratulation.resulting_mood = NPC_MOOD_IMPRESSED;
        congratulation.audio_source = s_npc.tower_reachable
            ? NPC_AUDIO_LIVE_TTS
            : NPC_AUDIO_SD_CONTEXTUAL;
        npc_build_sd_path(congratulation.sd_path, sizeof(congratulation.sd_path),
                          s_npc.current_scene, NPC_TRIGGER_QR_SCANNED,
                          NPC_MOOD_IMPRESSED, 0U);
        dispatch_decision(&congratulation);
    }
}

void npc_integration_on_phone_hook(bool off_hook)
{
    if (!s_initialised) return;

    const bool was_off_hook = s_npc.phone_off_hook;
    npc_on_phone_hook(&s_npc, off_hook);

    // Rising edge: phone just lifted.
    if (off_hook && !was_off_hook) {
        // If the player has been stuck, treat the lifted handset as a hint
        // request.  We use 0 for now_ms; the stuck guard in npc_evaluate
        // uses scene_start_ms which was set by npc_on_scene_change.
        // A proper now_ms would require plumbing millis() here; as the main
        // loop already calls npc_integration_tick every 5 s, the decision
        // will be caught there. For immediate response on phone lift while
        // stuck, also fire npc_on_hint_request directly.
        const uint32_t scene_elapsed = s_last_tick_ms - s_npc.scene_start_ms;
        if (scene_elapsed > NPC_STUCK_TIMEOUT_MS) {
            npc_on_hint_request(&s_npc, s_last_tick_ms);

            // Dispatch an immediate hint decision rather than waiting for
            // the next 5 s tick.
            npc_decision_t hint = {};
            hint.trigger       = NPC_TRIGGER_HINT_REQUEST;
            hint.resulting_mood = s_npc.mood;
            hint.audio_source  = s_npc.tower_reachable
                ? NPC_AUDIO_LIVE_TTS
                : NPC_AUDIO_SD_CONTEXTUAL;
            npc_build_sd_path(hint.sd_path, sizeof(hint.sd_path),
                              s_npc.current_scene, NPC_TRIGGER_HINT_REQUEST,
                              s_npc.mood,
                              npc_hint_level(&s_npc, s_npc.current_scene));
            dispatch_decision(&hint);
        }
    }
}

void npc_integration_reset(void)
{
    npc_reset(&s_npc);
    s_last_tick_ms = 0U;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/**
 * @brief Route an NPC decision to the appropriate audio subsystem.
 *
 * LIVE_TTS → audio_kit_play_tts() — Piper TTS on Tower via Audio Kit speaker.
 * SD_*     → audio_kit_play_sd()  — pre-generated MP3 on Audio Kit SD card.
 * NONE     → no-op.
 */
static void dispatch_decision(const npc_decision_t* decision)
{
    if (decision == nullptr) return;

    switch (decision->audio_source) {
        case NPC_AUDIO_LIVE_TTS:
            // phrase_text is populated by npc_evaluate() when the phrase bank
            // is loaded.  If empty (bank not yet wired), fall through to SD.
            if (decision->phrase_text[0] != '\0') {
                audio_kit_play_tts(decision->phrase_text, kTtsPiperUrl, kTtsVoice);
                break;
            }
            // Fallthrough: phrase bank not wired yet, use SD path if available.
            // [[fallthrough]];
            /* FALLTHROUGH */

        case NPC_AUDIO_SD_CONTEXTUAL:
        case NPC_AUDIO_SD_GENERIC:
            if (decision->sd_path[0] != '\0') {
                audio_kit_play_sd(decision->sd_path);
            }
            break;

        case NPC_AUDIO_NONE:
        default:
            break;
    }
}
