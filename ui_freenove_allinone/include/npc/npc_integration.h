// npc_integration.h - Wiring layer: NPC engine + TTS + QR + Audio Kit.
// Called from main.cpp event loop; keeps all NPC subsystem calls in one place.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all NPC subsystems.
 *
 * Must be called from setup() after WiFi is ready.
 * Initializes: NPC engine state, TTS client, Audio Kit HTTP client.
 *
 * @param audio_kit_base_url  e.g. "http://192.168.0.XXX:8300"
 */
void npc_integration_init(const char* audio_kit_base_url);

/**
 * @brief Periodic NPC tick — call from main loop every 5 s.
 *
 * Performs:
 *   - TTS health check
 *   - Tower status feed to NPC engine
 *   - Mood update
 *   - Decision evaluation and dispatch
 *
 * @param now_ms  Current millis() timestamp
 */
void npc_integration_tick(uint32_t now_ms);

/**
 * @brief Notify NPC of a scenario scene/step change.
 *
 * @param scene  Scene index (0-based position in ScenarioDef.steps[])
 * @param step   Step index within the scene (reserved, currently unused)
 */
void npc_integration_on_scene_change(uint8_t scene, uint8_t step);

/**
 * @brief Notify NPC of a QR scan result.
 *
 * @param payload  Raw QR payload string (may be NULL for invalid scans)
 */
void npc_integration_on_qr(const char* payload);

/**
 * @brief Notify NPC of telephone hook state change.
 *
 * Rising edge (false → true) while stuck triggers a hint request.
 *
 * @param off_hook  true = phone lifted, false = phone hung up
 */
void npc_integration_on_phone_hook(bool off_hook);

/**
 * @brief Reset NPC integration state for a new game session.
 *
 * Call alongside g_scenario.reset().
 */
void npc_integration_reset(void);

#ifdef __cplusplus
}
#endif
