# 🛠️ PLAN D'ACTION COURT TERME – Semaine 1
**Objectif**: Corriger les risques P0 (CRITIQUES) et P1 (HAUT) prioritaires.  
**Durée totale**: ~40 heures (5 jours)  
**Équipe**: 2 devs senior recommandé

---

## 📅 SEMAINE 1 – JOURS PAR JOUR

### LUNDI – Sécurité + Stabilité (8h)

#### 1️⃣ Audit + Triage (2h)
- [ ] Relire `/AUDIT_COMPLET_2026-03-01.md`
- [ ] Exécuter `grep -r "g_audio\|g_scenario\|g_ui" ui_freenove_allinone/src/main.cpp` → identifier 15+ race conditions
- [ ] Lister tous endpoints `/api/*` (via grep "server.on") → 40+ sans auth
- [ ] Output: `RACE_CONDITIONS.txt`, `API_ENDPOINTS.txt`

#### 2️⃣ WiFi Credentials Suppression (1h)
- [ ] Sauvegarder `data/story/apps/APP_WIFI.json` (backup local seulement)
- [ ] Remplacer valeurs credentials par placeholders:
  ```json
  {
    "local_ssid": "YOUR_SSID_HERE",
    "local_password": "YOUR_PASSWORD_HERE"
  }
  ```
- [ ] Commit: "Security: Remove hardcoded WiFi credentials"

#### 3️⃣ API Auth Layer Basique (3h)
- [ ] Créer `include/runtime/auth_service.h`:
  ```cpp
  class AuthService {
    static bool validateBearerToken(const String& auth_header);
    static String generateToken();
  };
  ```
- [ ] Ajouter check dans tous les handlers `/api/*`:
  ```cpp
  if (!AuthService::validateBearerToken(request->header("Authorization"))) {
    request->send(401);
    return;
  }
  ```
- [ ] Ajouter endpoint `/api/auth/token` (POST)
- [ ] Commit: "Feature: Bearer token authentication for web API"

#### 4️⃣ Watchdog Timer (2h)
- [ ] Ajouter dans `setup()` (main.cpp):
  ```cpp
  esp_task_wdt_init(5, true);  // 5-second timeout + auto-reboot
  esp_task_wdt_add_user_task(xTaskGetCurrentTaskHandle());
  ```
- [ ] Ajouter dans `loop()`:
  ```cpp
  static uint32_t last_wdt_reset = millis();
  if (millis() - last_wdt_reset > 1000) {
    esp_task_wdt_reset();
    last_wdt_reset = millis();
  }
  ```
- [ ] Test: Simuler hang (add infinite loop), vérifier auto-reboot
- [ ] Commit: "Feature: Add ESP32 Task Watchdog Timer (5s timeout)"

**Fin Lundi**: ✅ 3 commits sécurité + watchdog en place

---

### MARDI – Bug Critiques + Race Conditions (8h)

#### 5️⃣ Audio Memory Leak Fix (3h)
- [ ] Relire `audio_manager.cpp:159-160` playOnChannel()
- [ ] Remplacer:
  ```cpp
  // AVANT (BUG):
  AudioFileSource* source = new AudioFileSource(...);
  AudioGenerator* decoder = new AudioGenerator(...);
  if (!decoder) return false;  // leak!
  
  // APRÈS (FIX):
  auto source = std::make_unique<AudioFileSource>(...);
  auto decoder = std::make_unique<AudioGenerator>(...);
  if (!decoder) return false;  // auto-cleanup
  ```
- [ ] Chercher autres `new`/`delete` bruts → remplacer par `unique_ptr`
- [ ] Test: Jouer 100 pistes audio → vérifier mémoire stable
- [ ] Commit: "Fix: Audio memory leak using unique_ptr RAII pattern"

#### 6️⃣ Global State Mutex (4h)
- [ ] Créer `include/runtime/global_state.h`:
  ```cpp
  class GlobalState {
   private:
    SemaphoreHandle_t scenario_lock_;
    SemaphoreHandle_t audio_lock_;
    
   public:
    void lockScenario();
    void unlockScenario();
    void lockAudio();
    void unlockAudio();
  };
  
  extern GlobalState g_global_state;
  ```
- [ ] Wrapper: `ScopedGuard` RAII pour auto-unlock
- [ ] Appliquer sur:
  - `pollSerialCommands()` → acquire scenario_lock before modify
  - `loop()` → scenario.tick() avec reader-lock
  - Audio play/stop operations
- [ ] Test spinlock: Envoyer 100 serial commands rapidement → no corruption
- [ ] Commit: "Fix: Add mutex protection for global state (scenario, audio)"

#### 7️⃣ Serial Buffer Validation (1h)
- [ ] Main.cpp pollSerialCommands():
  ```cpp
  size_t bytes = Serial.readBytesUntil('\n', g_serial_line, sizeof(g_serial_line)-1);
  if (bytes >= sizeof(g_serial_line)-1) {
    Serial.println("ERR: Command line too long (>191 chars)");
    return;
  }
  ```
- [ ] Fuzz test: Envoyer 500+ char line → should reject gracefully
- [ ] Commit: "Fix: Add serial buffer overflow protection"

**Fin Mardi**: ✅ 3 commits bugs critiques + mutex en place

---

### MERCREDI – Refactor + Path Traversal (8h)

#### 8️⃣ Path Traversal Sanitization (2h)
- [ ] Créer `include/storage/path_validator.h`:
  ```cpp
  class PathValidator {
   public:
    static bool isSafe(const char* path);
    // Returns false if path contains ../  or starts with /
  };
  ```
- [ ] Utiliser dans `storage_manager.cpp`:
  ```cpp
  if (!PathValidator::isSafe(path)) {
    Serial.println("ERR: Path traversal attempt blocked");
    return "";
  }
  ```
- [ ] Test: Essayer `/../../etc/passwd` → blocked
- [ ] Commit: "Fix: Path traversal vulnerability mitigation"

#### 9️⃣ Serial Command Handler Refactor (6h) 
**Objectif**: Réduire cyclomatic complexity de 50+ à <20

- [ ] Créer `include/runtime/serial_command_map.h`:
  ```cpp
  using CommandHandler = std::function<void(const char* args, uint32_t now_ms)>;
  
  struct SerialCommandDispatcher {
    static std::map<String, CommandHandler> commands_;
    
    static bool dispatch(const String& cmd, const String& args, uint32_t now_ms) {
      auto it = commands_.find(cmd);
      if (it == commands_.end()) return false;
      it->second(args.c_str(), now_ms);
      return true;
    }
  };
  ```
  
- [ ] Extraire handlers du giant switch:
  ```cpp
  // Avant: 50+ case statements en handleSerialCommand()
  
  // Après: separate files
  // serial/cmd_wifi.cpp
  void cmdWifiStatus(const char* args, uint32_t now_ms) { ... }
  void cmdWifiConnect(const char* args, uint32_t now_ms) { ... }
  
  // serial/cmd_audio.cpp
  void cmdAudioPlay(const char* args, uint32_t now_ms) { ... }
  void cmdAudioStop(const char* args, uint32_t now_ms) { ... }
  ```

- [ ] Register handlers dalam `setup()`:
  ```cpp
  SerialCommandDispatcher::register("WIFI_STATUS", cmdWifiStatus);
  SerialCommandDispatcher::register("WIFI_CONNECT", cmdWifiConnect);
  SerialCommandDispatcher::register("AUDIO_PLAY", cmdAudioPlay);
  ```

- [ ] Update `handleSerialCommand()`:
  ```cpp
  bool handleSerialCommand(const char* cmd, const char* args, uint32_t now_ms) {
    return SerialCommandDispatcher::dispatch(cmd, args, now_ms);
  }
  ```

- [ ] Complexity check: `clang-tidy main.cpp` → should report <20 cyclo now
- [ ] Commit: "Refactor: Modularize serial command handler (50+ → <10 cyclo)"

**Fin Mercredi**: ✅ Path traversal fix + command handler modularized

---

### JEUDI – Sécurité Avancée + Docs (8h)

#### 🔟 JSON Validation Schema (2h)
- [ ] Créer `include/storage/json_schema_validator.h`:
  ```cpp
  class JsonValidator {
   public:
    static bool validateScenario(const JsonDocument& doc, String* out_error);
    static bool validateApp(const JsonDocument& doc, String* out_error);
  };
  ```
  
- [ ] Utiliser avant deserialize:
  ```cpp
  DynamicJsonDocument doc(file_size);
  deserializeJson(doc, file);
  String error;
  if (!JsonValidator::validateScenario(doc, &error)) {
    Serial.printf("JSON validation failed: %s\n", error.c_str());
    return false;
  }
  ```

- [ ] Add size limits:
  ```cpp
  #define JSON_MAX_SCENARIO_SIZE 12288
  #define JSON_MAX_APP_SIZE 8192
  if (file_size > JSON_MAX_SCENARIO_SIZE) return false;
  ```

- [ ] Commit: "Feature: JSON schema validation with size limits"

#### 1️⃣1️⃣ Telemetry Task (2h)
- [ ] Créer `src/runtime/telemetry_task.cpp`:
  ```cpp
  void telemetryTask(void* arg) {
    while (true) {
      uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      uint32_t lv_mem = lv_mem_get_usage();
      
      Serial.printf("[TELEMETRY] int=%u psram=%u lvgl=%u\n", 
        free_internal, free_psram, lv_mem);
      
      vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10s
    }
  }
  ```
  
- [ ] Create dans setup:
  ```cpp
  xTaskCreatePinnedToCore(telemetryTask, "telemetry", 2048, nullptr, 1, nullptr, 0);
  ```

- [ ] Monitor: 10h runtime → should not decrease below 80KB internal heap
- [ ] Commit: "Feature: Telemetry task for memory monitoring"

#### 1️⃣2️⃣ Documentation (4h)
**Create 2 docs**:

1️⃣ Create `docs/ARCHITECTURE.md`:
```markdown
# Architecture ESP32_ZACUS

## High-Level Overview
- Main components: ui_manager, scenario_manager, audio_manager, network_manager
- FreeRTOS tasks: audioPumpTask, btn_scanTask, ui_task (optional)
- Global state: Protected by mutex (g_scenario, g_audio)

## Data Flow
Serial command → main.cpp → dispatchScenarioEventByName → scenario.notifyButton
→ scenario state change → ui.tick() reads snapshot → LVGL render

## Security Model
- Bearer token auth on all `/api/*`
- Path validation for file operations
- Size limits on JSON parsing

[Include diagrams]
```

2️⃣ Create `docs/SECURITY.md`:
```markdown
# Security Status & Alerts

## Current Issues (As of 2026-03-01)

### Fixed ✅
- [x] WiFi credentials moved to NVS
- [x] API authentication (Bearer token)
- [x] Serial buffer overflow validation
- [x] Path traversal sanitization

### In Progress 🔄
- [ ] LVGL re-entrancy (core dedication)
- [ ] Memory leak detection automation

### Known Limitations ⚠️
- No HTTPS/TLS (embedded device constraint)
- Token stored in plain SRAM (no secure enclave)

[Full detailed list]
```

3️⃣ Update `README_ESP32_ZACUS.md`:
- Remove old "KO reboot loop" section
- Add: "✅ Runtime stable (watchdog enabled, mutex protected)"
- Add link to AUDIT_COMPLET_2026-03-01.md

- Commit: "Docs: Add ARCHITECTURE, SECURITY, update README"

**Fin Jeudi**: ✅ JSON validation + telemetry + docs

---

### VENDREDI – Test + Review (8h)

#### 1️⃣3️⃣ Integration Test (3h)
- [ ] Créer `test/integration_test_wifi_scenario.py`:
  ```python
  def test_api_auth_required():
    # Must fail without Bearer token
    resp = requests.get("http://esp32:80/api/apps/list")
    assert resp.status_code == 401
    
    # Must succeed WITH token
    resp = requests.get("http://esp32:80/api/apps/list",
      headers={"Authorization": f"Bearer {token}"})
    assert resp.status_code == 200
  
  def test_serial_command_race():
    # Send 10 sc_load commands in parallel
    # Verify no corruption, all succeed
  
  def test_memory_no_leak_4h():
    # Run 4 scenarios 100 times = 4h equivalent
    # Verify free heap doesn't drop below 80KB
  ```

- [ ] Run locally:
  ```bash
  python test/integration_test_wifi_scenario.py --port /dev/cu.usbmodem --duration 4h
  ```

- [ ] Commit: "Test: Add integration tests for security + stability"

#### 1️⃣4️⃣ Code Review Preparation (3h)
- [ ] Create PR checklist:
  ```
  - [ ] All commits have clear messages
  - [ ] No security vulns introduced
  - [ ] Memory leaks fixed (unique_ptr, RAII)
  - [ ] Mutex added (scenario + audio)
  - [ ] Watchdog enabled
  - [ ] Auth on all /api/*
  - [ ] Path validation in storage
  - [ ] JSON size limits
  - [ ] Test results attached
  ```

- [ ] Self-review all 7 commits:
  ```bash
  git log --oneline HEAD~7..HEAD
  git show <commit>  # Review each one
  ```

- [ ] Capture screenshots:
  - Watchdog reboot on hang
  - Auth 401 without token
  - Memory telemetry stable 4h

#### 1️⃣5️⃣ Final Validation (2h)
- [ ] Fresh build:
  ```bash
  pio clean
  pio run -e freenove_esp32s3_full_with_ui
  pio run -e freenove_esp32s3_full_with_ui -t buildfs
  pio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port /dev/cu.usbmodem
  pio run -e freenove_esp32s3_full_with_ui -t upload --upload-port /dev/cu.usbmodem
  ```

- [ ] Serial smoke:
  ```bash
  python lib/zacus_story_portable/test_story_4scenarios.py --port /dev/cu.usbmodem
  # Must pass all 4 scenarios without crash
  ```

- [ ] Manual tests:
  - Boot → Watchdog logging visible
  - Serial command auth fails if no token
  - API endpoints require Bearer auth
  - Memory stable (check telemetry logs)

**Fin Vendredi**: ✅ Tests passing + PR ready for review

---

## 📊 Daily Status Table

|   | Lundi | Mardi | Mercredi | Jeudi | Vendredi |
|---|-------|-------|----------|-------|----------|
| **Security** | ✅ WiFi, Auth, Buffer | ✅ Watchdog | ✅ Path validation | ✅ JSON validation | ✅ Verified |
| **Stability** | - | ✅ Memory leak, Mutex | - | ✅ Telemetry | ✅ Tested |
| **Refactor** | - | - | ✅ Serial handler | - | ✅ Code review |
| **Docs** | - | - | - | ✅ ARCHITECTURE, SECURITY | - |
| **Tests** | - | - | - | - | ✅ Integration, Smoke |
| **Commits** | 3 | 3 | 1 | 2 | 1 |

---

## 💾 GIT Commit Messages (Copy-Paste Ready)

```bash
# Lundi
git commit -m "Security: Remove hardcoded WiFi credentials"
git commit -m "Feature: Bearer token authentication for web API"
git commit -m "Feature: Add ESP32 Task Watchdog Timer (5s timeout)"

# Mardi
git commit -m "Fix: Audio memory leak using unique_ptr RAII pattern"
git commit -m "Fix: Add mutex protection for global state (scenario, audio)"
git commit -m "Fix: Add serial buffer overflow protection"

# Mercredi
git commit -m "Fix: Path traversal vulnerability mitigation"
git commit -m "Refactor: Modularize serial command handler (50+ → <10 cyclo)"

# Jeudi
git commit -m "Feature: JSON schema validation with size limits"
git commit -m "Feature: Telemetry task for memory monitoring"
git commit -m "Docs: Add ARCHITECTURE, SECURITY, update README"

# Vendredi
git commit -m "Test: Add integration tests for security + stability"
```

---

## ✅ Success Criteria (EOD Vendredi)

- [ ] 0 open security vulns (CRITICAL/HIGH)
- [ ] Memory stable 4+ hours (telemetry log)
- [ ] All 4 scenarios pass smoke test 10x
- [ ] API responds 401 without Bearer token
- [ ] Serial command handler cyclo <20
- [ ] 7+ commits with clear messages
- [ ] 3 new docs (ARCHITECTURE, SECURITY, updated README)
- [ ] PR ready for review with all tests passing

---

## 🤝 Hand-off to Team

### Next Steps (Semaine 2)
- [ ] Code review (2h senior dev)
- [ ] Security audit on HTTP layer
- [ ] Merge PR to main
- [ ] Tag release v1.1.0-security

### For Phase 2 (Weeks 3-4)
- [ ] LVGL re-entrancy (core dedication)
- [ ] Network async state machine
- [ ] Unit tests (gtest setup)

---

Bonne chance! 💪
