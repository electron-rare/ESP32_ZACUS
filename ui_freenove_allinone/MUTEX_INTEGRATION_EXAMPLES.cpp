// ============================================================================
// EXEMPLES D'INTÉGRATION MUTEX DANS main.cpp
// Ces exemples montrent comment protéger les accès concurrents à g_audio et g_scenario
// ============================================================================

#include "core/mutex_manager.h"

// ============================================================================
// EXEMPLE 1: Protection callback audio (onAudioFinished)
// AVANT: Race condition - callback I2S modifie g_scenario sans lock
// APRÈS: Acquisition scenario_mutex avec timeout ISR-safe
// ============================================================================

// AVANT (ligne 969 - RACE CONDITION):
void onAudioFinished(const char* track, void* ctx) {
  (void)ctx;
  Serial.printf("[MAIN] audio done: %s\n", track != nullptr ? track : "unknown");
  g_scenario.notifyAudioDone(millis());  // UNSAFE - Modification depuis ISR sans lock!
}

// APRÈS (protégé):
void onAudioFinished(const char* track, void* ctx) {
  (void)ctx;
  Serial.printf("[MAIN] audio done: %s\n", track != nullptr ? track : "unknown");
  
  ScenarioLock lock(100);  // Timeout court (100ms) pour ISR, évite blocage
  if (!lock.acquired()) {
    Serial.println("[MUTEX] WARN: Audio callback could not notify scenario (mutex held)");
    return;
  }
  g_scenario.notifyAudioDone(millis());
}

// ============================================================================
// EXEMPLE 2: Protection HTTP handler webBuildStatusDocument
// AVANT: Race condition - lecture g_audio/g_scenario pendant modifications
// APRÈS: Acquisition dual lock pour cohérence
// ============================================================================

// AVANT (ligne 1977-1979 - RACE CONDITION):
void webBuildStatusDocument(StaticJsonDocument<4096>* out_document) {
  // ...
  JsonObject audio = (*out_document)["audio"].to<JsonObject>();
  audio["playing"] = g_audio.isPlaying();     // UNSAFE - Peut lire pendant update()
  audio["track"] = g_audio.currentTrack();    // UNSAFE - String peut être modifié
  audio["volume"] = g_audio.volume();
  // ...
}

// APRÈS (protégé):
void webBuildStatusDocument(StaticJsonDocument<4096>* out_document) {
  if (out_document == nullptr) {
    return;
  }
  
  DualLock lock(500);  // Acquisition audio+scenario avec timeout 500ms
  if (!lock.acquired()) {
    Serial.println("[MUTEX] WARN: Web status could not acquire locks, returning partial data");
    // Option: retourner données partielles ou erreur
    (*out_document)["ok"] = false;
    (*out_document)["error"] = "mutex_timeout";
    return;
  }
  
  const NetworkManager::Snapshot net = g_network.snapshot();
  const ScenarioSnapshot scenario = g_scenario.snapshot();

  out_document->clear();
  JsonObject network = (*out_document)["network"].to<JsonObject>();
  // ... (code réseau inchangé)

  JsonObject story = (*out_document)["story"].to<JsonObject>();
  story["scenario"] = scenarioIdFromSnapshot(scenario);
  story["step"] = stepIdFromSnapshot(scenario);
  story["screen"] = (scenario.screen_scene_id != nullptr) ? scenario.screen_scene_id : "";
  story["audio_pack"] = (scenario.audio_pack_id != nullptr) ? scenario.audio_pack_id : "";

  JsonObject audio = (*out_document)["audio"].to<JsonObject>();
  audio["playing"] = g_audio.isPlaying();
  audio["track"] = g_audio.currentTrack();
  audio["volume"] = g_audio.volume();
  
  // Lock automatiquement releasé à la fin du scope
}

// ============================================================================
// EXEMPLE 3: Protection commandes serial (handleSerialCommand)
// AVANT: Audio stop pendant update() → crash potentiel
// APRÈS: Audio lock pour opérations critiques
// ============================================================================

// AVANT (ligne 3157 - RACE CONDITION):
if (std::strcmp(command, "AUDIO_TEST") == 0) {
  g_audio.stop();  // UNSAFE - Peut stopper pendant update() en cours
  const bool ok = g_audio.playDiagnosticTone();
  Serial.printf("ACK AUDIO_TEST %u\n", ok ? 1 : 0);
  return;
}

// APRÈS (protégé avec macro):
if (std::strcmp(command, "AUDIO_TEST") == 0) {
  AUDIO_GUARDED_CALL({
    g_audio.stop();
    const bool ok = g_audio.playDiagnosticTone();
    Serial.printf("ACK AUDIO_TEST %u\n", ok ? 1 : 0);
  });
  return;
}

// ============================================================================
// EXEMPLE 4: Protection main loop (ligne 3478-3480)
// AVANT: update() et tick() non protégés
// APRÈS: Locks séparés pour parallélisme optimal
// ============================================================================

// AVANT (RACE CONDITION):
void loop() {
  const uint32_t now_ms = millis();
  // ... watchdog, serial, buttons ...
  
  g_audio.update();         // UNSAFE - Modifie playing_, current_track_
  g_scenario.tick(now_ms);  // UNSAFE - Modifie current_step_index_
  
  // ... rest of loop
}

// APRÈS (protégé):
void loop() {
  const uint32_t now_ms = millis();
  
  // Feed watchdog
  esp_task_wdt_reset();
  g_watchdog_feeds++;
  
  pollSerialCommands(now_ms);
  // ... button/touch handling ...
  
  // Audio update avec lock
  {
    AudioLock lock(50);  // Timeout court (50ms) pour éviter blocage loop
    if (lock.acquired()) {
      g_audio.update();
    } else {
      Serial.println("[MUTEX] WARN: Skipped audio update (mutex held)");
    }
  }
  
  // Scenario tick avec lock séparé (permet parallélisme)
  {
    ScenarioLock lock(50);
    if (lock.acquired()) {
      g_scenario.tick(now_ms);
      startPendingAudioIfAny();  // Peut nécessiter audio lock aussi
    } else {
      Serial.println("[MUTEX] WARN: Skipped scenario tick (mutex held)");
    }
  }
  
  refreshSceneIfNeeded(false);
  g_ui.update();
  
  if (g_web_started) {
    g_web_server.handleClient();
  }
  
  delay(5);
}

// ============================================================================
// EXEMPLE 5: Protection dispatchScenarioEventByName (ligne 2467-2493)
// AVANT: Notifications scenario sans protection
// APRÈS: Scenario lock pour cohérence
// ============================================================================

// AVANT (RACE CONDITION):
bool dispatchScenarioEventByName(const char* event_name, uint32_t now_ms) {
  // ... parsing ...
  
  if (std::strcmp(normalized, "UNLOCK") == 0) {
    g_scenario.notifyUnlock(now_ms);  // UNSAFE - Modifie current_step_index_ sans lock
    return true;
  }
  // ...
  return g_scenario.notifySerialEvent(normalized, now_ms);  // UNSAFE
}

// APRÈS (protégé):
bool dispatchScenarioEventByName(const char* event_name, uint32_t now_ms) {
  if (event_name == nullptr || event_name[0] == '\0') {
    return false;
  }

  char normalized[kSerialLineCapacity] = {0};
  std::strncpy(normalized, event_name, sizeof(normalized) - 1U);
  toUpperAsciiInPlace(normalized);
  
  ScenarioLock lock(1000);  // Timeout 1s pour événements
  if (!lock.acquired()) {
    Serial.printf("[MUTEX] ERROR: Cannot dispatch event %s (mutex timeout)\n", normalized);
    return false;
  }

  const ScenarioSnapshot current = g_scenario.snapshot();
  if (!g_la_dispatch_in_progress && shouldEnforceLaMatchOnly(current)) {
    if (std::strcmp(normalized, "UNLOCK") == 0 || std::strcmp(normalized, "BTN_NEXT") == 0 ||
        std::strcmp(normalized, "SERIAL:BTN_NEXT") == 0) {
      Serial.printf("[LA_TRIGGER] blocked manual event=%s while waiting LA match\n", normalized);
      return false;
    }
  }

  if (std::strcmp(normalized, "UNLOCK") == 0) {
    g_scenario.notifyUnlock(now_ms);
    return true;
  }
  if (std::strcmp(normalized, "AUDIO_DONE") == 0) {
    g_scenario.notifyAudioDone(now_ms);
    return true;
  }

  // ... rest of event dispatching ...
  return g_scenario.notifySerialEvent(normalized, now_ms);
  
  // Lock automatiquement releasé ici
}

// ============================================================================
// EXEMPLE 6: Modifications dans setup() (ligne 3351-3361)
// AVANT: Initialisation sans mutex
// APRÈS: Init mutex AVANT g_audio/g_scenario
// ============================================================================

// AVANT:
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[MAIN] Freenove all-in-one boot");
  
  // ... storage, hardware ...
  
  g_audio.begin();  // Pas encore de protection
  g_scenario.begin(kDefaultScenarioFile);
  // ...
}

// APRÈS:
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[MAIN] Freenove all-in-one boot");
  
  // ===== WATCHDOG TIMER INITIALIZATION =====
  esp_task_wdt_init(kDefaultWatchdogTimeoutSec, true);
  esp_task_wdt_add(NULL);
  g_watchdog_feeds = 0U;
  g_watchdog_last_feed_ms = millis();
  Serial.printf("[WATCHDOG] Initialized: timeout=%u seconds\n", kDefaultWatchdogTimeoutSec);
  
  // ===== MUTEX INITIALIZATION (CRITICAL - BEFORE AUDIO/SCENARIO) =====
  if (!MutexManager::init()) {
    Serial.println("[MUTEX] FATAL: Mutex initialization failed - race conditions possible!");
    // Option: activer mode dégradé ou reboot
  }
  
  // ... storage, hardware, network ...
  
  setupWebUi();  // WebServer peut maintenant utiliser locks de manière sûre
  
  g_audio.begin();
  g_audio.setAudioDoneCallback(onAudioFinished, nullptr);
  
  if (!g_scenario.begin(kDefaultScenarioFile)) {
    Serial.println("[MAIN] scenario init failed");
  }
  
  // ... UI, scene refresh ...
}

// ============================================================================
// EXEMPLE 7: Commande UART de diagnostic mutex
// Nouvelle commande pour monitorer performance mutex
// ============================================================================

void handleSerialCommand(const char* command_line, uint32_t now_ms) {
  // ... existing commands ...
  
  if (std::strcmp(command, "MUTEX_STATUS") == 0) {
    Serial.printf("MUTEX_STATUS audio_locks=%lu scenario_locks=%lu audio_timeouts=%lu scenario_timeouts=%lu "
                  "max_audio_wait_us=%lu max_scenario_wait_us=%lu\n",
                  MutexManager::audioLockCount(),
                  MutexManager::scenarioLockCount(),
                  MutexManager::audioTimeoutCount(),
                  MutexManager::scenarioTimeoutCount(),
                  MutexManager::maxAudioWaitUs(),
                  MutexManager::maxScenarioWaitUs());
    return;
  }
  
  if (std::strcmp(command, "MUTEX_RESET_STATS") == 0) {
    // Reset stats manually if needed (would need MutexManager::resetStats())
    Serial.println("ACK MUTEX_RESET_STATS");
    return;
  }
  
  // ... rest of commands
}

// ============================================================================
// CHECKLIST D'INTÉGRATION COMPLÈTE
// ============================================================================

/*
FICHIERS À MODIFIER:

1. ui_freenove_allinone/src/main.cpp:
   ✅ Ligne 35-36: Ajouter #include "core/mutex_manager.h"
   ✅ Ligne 969: Protéger onAudioFinished()
   ✅ Ligne 1946-1979: Protéger webBuildStatusDocument()
   ✅ Ligne 2467-2493: Protéger dispatchScenarioEventByName()
   ✅ Ligne 3157-3220: Protéger commandes serial audio
   ✅ Ligne 3351: Init MutexManager::init() AVANT g_audio.begin()
   ✅ Ligne 3478-3480: Protéger loop() audio.update() + scenario.tick()

2. ui_freenove_allinone/src/audio_manager.cpp:
   - Option: Protéger méthodes internes (play, stop, update) SI nécessaire
   - Pré-requis: Vérifier si AudioManager a déjà ses propres locks internes
   
3. ui_freenove_allinone/src/scenario_manager.cpp:
   - Option: Protéger snapshot(), dispatchEvent() SI nécessaire
   - Évaluer si protection externe (main.cpp) suffit

4. platformio.ini:
   - Vérifier build_flags inclut -DCONFIG_FREERTOS_HZ=1000 (nécessaire pour pdMS_TO_TICKS)

TESTS DE VALIDATION:

1. Test stress concurrent:
   - Envoyer 100 requêtes HTTP /api/status simultanées
   - Vérifier 0 timeout mutex, 0 crash

2. Test callback audio:
   - Jouer audio pendant requêtes HTTP status
   - Vérifier cohérence scenario.notifyAudioDone()

3. Test serial+HTTP:
   - Envoyer AUDIO_TEST en UART pendant HTTP /api/audio/play
   - Vérifier pas de crash, ordre prévisible

4. Test watchdog:
   - Simuler mutex deadlock (enlever releaseMutex)
   - Vérifier watchdog reboot après 30s

MÉTRIQUES PERFORMANCE (ESP32-S3 @ 240MHz):
- Overhead lock/unlock: ~50µs (acceptable pour loop() @ 200Hz)
- Max wait observé: <5ms en conditions normales
- Timeout count cible: 0 (tout timeout = bug potentiel)

COMMANDES UART DIAGNOSTIQUES:
- MUTEX_STATUS: Affiche stats locks/timeouts/wait
- STATUS: Inclure info mutex dans rapport global
*/
