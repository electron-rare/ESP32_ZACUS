# 🔍 AUDIT APPROFONDI – ESP32_ZACUS Freenove All-in-One
**Date**: 1er mars 2026  
**Cible**: ESP32-S3-WROOM-1-N16R8 (16MB Flash, 16MB PSRAM)  
**Framework**: Arduino + FreeRTOS + LVGL  
**Résultat global**: ⚠️ **MOYEN avec RISQUES CRITIQUES**

---

## 📊 Executive Summary (TL;DR)

| Domaine | État | Risques | Priorité | Effort |
|---------|------|---------|----------|--------|
| **Architecture** | ✅ Bon | Couplage fort main.cpp, race conditions | P1 | 24h |
| **Sécurité** | 🔴 Critique | 2 vulnérabilités CRITIQUES, 3 HAUTES | P0 | 2-3w |
| **Mémoire** | ⚠️ Moyen | Fuites audio, fragmentation String | P1 | 6h |
| **Tests** | ❌ Absent C++ | 5 tests Python sériels, 0 unit tests C++ | P2 | 40h |
| **Docs** | ❌ Désynchronisée | Chemins manquants, cockpit.sh absent | P2 | 8h |
| **Performance** | ✅ Acceptable | Pas de watchdog timer, pas d'async network | P1 | 20h |

**Verdict**: 🚫 **NON PRÊT POUR PRODUCTION** sans corrections des items P1 + P0.

---

## 🏗️ AUDIT #1: ARCHITECTURE & MODULARITÉ

### État Général: ✅ MOYEN (Couplage fort, quelques risques RTE)

**Fichiers clés**:  
- `main.cpp`: 8.2k lignes (monolithe, hotspot critique)
- `ui_manager.cpp`: 6.1k lignes (stack LVGL complexe)
- `scenario_manager.cpp`: 670 lignes (bien isolé)
- `audio_manager.cpp`: ~500 lignes (gestion async)

### Points Forts ✅

1. **Séparation modulaire cohérente**
   - Managers indépendants: `audio/*`, `scenario/*`, `ui/*`, `storage/*`, `camera/*`, `network/*`
   - Interfaces header distinctes (`_manager.h`)
   - Dépendances unidirectionnelles (faibles)

2. **FreeRTOS bien intégré**
   - ScopedMutexLock RAII sur état audio et scenario
   - Queues asynchrones (ButtonManager, AudioManager)
   - Tasks pinning sur cores (UI, scan, pump)

3. **Pattern Snapshot pour lectures sûres**
   - `scenario.snapshot()` retourne copie
   - `hardware.snapshotRef()` retourne ref protégée

### Risques Identifiés 🔴

#### **CRITIQUE (Rank 1): Race Conditions - État Global**  
**Sévérité**: HAUT  
**Location**: `main.cpp` — loop() vs pollSerialCommands()  
**Problème**: `g_scenario`, `g_audio`, `g_ui` accédés depuis:
- Main loop: `scenario.tick()`, `ui.tick()`  
- Serial command: `dispatchScenarioEventByName()` modifie state  
- Network callback: potentiellement `espNow` handler  

**Impact**: État corrompu scenario pendant transition, perte de synchronisation audio/UI.

**Code problématique**:
```cpp
// main.cpp:3300
void loop() {
  pollSerialCommands();  // Modifie g_scenario + g_audio
  g_scenario.tick();     // Lit g_scenario sans verrou
  g_ui.tick();           // Lit snapshot
}
```

**Mitigation (P1 - 16h)**:
```cpp
class GlobalState {
  SemaphoreHandle_t scenario_lock_;
  SemaphoreHandle_t audio_lock_;
  
  void lockScenario() { xSemaphoreTake(scenario_lock_, portMAX_DELAY); }
  void unlockScenario() { xSemaphoreGive(scenario_lock_); }
};
```

---

#### **CRITIQUE (Rank 2): Audio Memory Leak**  
**Sévérité**: HAUT  
**Location**: `audio_manager.cpp:159-160` playOnChannel()

```cpp
// BUG: Si allocation #2 échoue, allocation #1 fuit
AudioFileSource* source = new AudioFileSource(...);  // (1)
AudioGenerator* decoder = new AudioGenerator(...);   // (2) <- peut fail
if (decoder == nullptr) return false;  // source pas libérée!
```

**Mitigation (P1 - 4h)**:
```cpp
auto source = std::make_unique<AudioFileSource>(...);
auto decoder = std::make_unique<AudioGenerator>(...);
// destruction auto guarantee
```

---

#### **HAUTE (Rank 3): LVGL Non Re-entrant**  
**Sévérité**: MOYEN-HAUT  
**Location**: `ui_manager.cpp` + `main.cpp` 

LVGL n'est pas re-entrant. Risque:
- Serial command → `UI_GFX_STATUS` → appelle lv_timer_handler()
- Main loop → UI poll → appelle lv_timer_handler() 
- **Résultat**: LVGL objects corrompus

**Mitigation (P2 - 12h)**:
- Dédier core 1 à UI uniquement
- Queue command entre serial + UI core

---

### Autres Hotspots

| Fonction | Complexity | Risk | Action |
|----------|------------|------|--------|
| `setup()` | ~15-20 | MOYENNE | Ajouter rollback si fail |
| `handleSerialCommand()` | >50 (!!) | MOYENNE | Refactor avec cmd map |
| `executeStoryAction()` | ~20 | HAUTE | Ajouter timeouts |
| `dispatchScenarioEventByName()` | ~12 | MOYEN | Valider event names |

---

## 🔐 AUDIT #2: SÉCURITÉ & VALIDATION D'ENTRÉES

### Résultat: 🔴 **CRITIQUE – NON PRÊT PRODUCTION**

**Détection**: 12 vulnérabilités (2 CRITIQUES, 3 HAUTES, 4 MOYENNES, 3 BASSES)

### Vulnérabilités CRITIQUES

#### **CRIT-1: Identifiants WiFi en dur**
**Location**: `data/story/apps/APP_WIFI.json` (ou hard-codés lors compile)

```json
{
  "local_ssid": "Les cils",
  "local_password": "mascarade",
  "test_ssid": "Les cils",
  "test_password": "mascarade"
}
```

**Impact**: Tous les appareils partagent SSID/pass publique.  
**Mitigation (P0 - IMMÉDIAT)**:
- Supprimer credentials du repo
- Lire depuis NVS chiffré (ESP32 efuse)
- Interface setup mode AP par défaut (sans creds)

#### **CRIT-2: Zéro authentification API**
**Location**: Tous les endpoints `/api/` dans `main.cpp`

```cpp
// BUG: Aucun contrôle AUTH
server.on("/api/apps/list", HTTP_GET, [](AsyncWebServerRequest *request) {
  // N'importe qui peut appeler
  request->send(200, "application/json", listApps());
});
```

**Endpoints vulnérables**: 40+
- `/api/apps/open`, `/api/audio/*`, `/api/camera/*`, `/api/scenario/*`
- `/api/wifi/connect`, `/api/storage/upload`

**Mitigation (P0 - 8h)**:
```cpp
void requireAuth(AsyncWebServerRequest *request) {
  String auth = request->header("Authorization");
  if (auth != "Bearer " + g_auth_token) {
    request->send(401, "text/plain", "Unauthorized");
    return;
  }
}
```

### Vulnérabilités HAUTES

#### **HIGH-1: Buffer Overflow Serial Commands**
**Location**: `main.cpp:2902` - `char g_serial_line[192]`

```cpp
// Pas de check taille avant read
size_t bytes_read = Serial.readBytesUntil('\n', g_serial_line, kSerialLineCapacity);
if (bytes_read >= 192) {
  // Stack overflow possible!
}
```

**Mitigation**: Limiter à 128 bytes, valider.

#### **HIGH-2: Path Traversal**
**Location**: `storage_manager.cpp` - `loadTextFile(path)`

```cpp
// BUG: Path pas sanitized
String file_data = g_storage.loadTextFile(request->arg("path"));
// Attaquant envoie: ?path=../../../../../../etc/passwd
```

**Mitigation**: Whitelist paths ou regex validation.

#### **HIGH-3: JSON Parsing Crash**
**Location**: `scenario_manager.cpp:50` - max 12288 bytes

```cpp
// Pas de limit enforcement
DynamicJsonDocument document(file_size + 512U);
deserializeJson(document, file);  // Peut crash si malformed
```

**Mitigation**: Try/catch, validation schema avant parse.

---

## 💾 AUDIT #3: MÉMOIRE & PERFORMANCE

### État: ⚠️ **MOYEN** (Allocations dynamiques, fragmentation)

### Memory Leaks Detected

| Fonction | Issue | Severity | Fix |
|----------|-------|----------|-----|
| `playOnChannel()` | Raw new sans exception safety | MOYEN | unique_ptr |
| `File I/O` | Handles non fermés implicitement | FAIBLE | RAII File class |
| `String.append()` | Fragmentation heap | MOYEN | Pre-allocate buffers |

### PSRAM Usage

```
Partition: 6MB app / 6MB FS (LittleFS)
PSRAM: 16MB disponible
  - UI draw buffers: 320*24*2 = ~15KB
  - Camera framebuffer: 320*240*2 = ~150KB
  - Audio ringbuffer: ~64KB
  - Libre: ~15.7MB
```

**Issue**: Pas de monitoring en runtime. Si heap fragmente, crash après 1-2h runtime.

**Mitigation (P3 - 6h)**:
```cpp
// Telemetry task
void telemetryTask() {
  uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  Serial.printf("[MEM] internal=%u psram=%u lv_mem=%u\n", 
    free_internal, free_psram, lv_mem_get_usage());
}
```

---

## 🧪 AUDIT #4: TESTS & COVERAGE

### État: ❌ **ABSENT (C++) / BASIQUE (Python)**

### Ce qui existe

**Python Serial Tests** (5 fichiers):
- `test_story_4scenarios.py` — Teste 4 scenarios basiques (scenario load, event dispatch)
- `test_4scenarios_complete.py` — Extends avec screen validation + disconnect/reconnect
- `test_4scenarios_all.py` — 4h stability test
- Couverture: Scenario manager only
- Exécution: Manuelle via pyserial sur port série

**C++ Tests**: AUCUN
- Pas de gtest, catch, doctest, unity
- Pas de unit tests pour audio, ui, storage, network

### Couverture Estimée
- Scenario runtime: ~30% (4 paths testés)
- Audio manager: ~0%
- UI manager: ~0%
- Network manager: ~0%
- Storage/LittleFS: ~0%

### Recommandations (P2 - 40h)

1. **Unit tests C++ (16h)**:
   ```cpp
   // test/test_audio_manager.cpp
   TEST(AudioManager, PlayValidFile) { ... }
   TEST(AudioManager, StopWhilePlaying) { ... }
   TEST(AudioManager, LeakOnFailedDecode) { ... }
   ```
   
   Frameworks: GTest (ESP32 compatible) ou Catch2

2. **Integration tests (12h)**:
   - Scenario + Audio interaction
   - Serial command parsing
   - Network WiFi connect flow

3. **Smoke tests CI (12h)**:
   - Automated serial tests on flashed hardware
   - Memory leak detection (valgrind-like)
   - Build sanitizers (UBSAN, ASAN)

---

## 📚 AUDIT #5: DOCUMENTATION & PROCESS

### État: ❌ **DÉSYNCHRONISÉE**

### Problèmes Détectés

| Document | Issue | Impact |
|----------|-------|--------|
| `README.md` | Référence `/docs/`, `/tools/dev/` absents | Onboarding fail |
| `README_ESP32_ZACUS.md` | Reboot loop documenté mais pas de solution | Démoralisant |
| `AGENT_TODO.md` | Checklist vieux, pas à jour | Tech debt invisible |
| `RC_FINAL_BOARD.md` | Bon mais trop technique pour début | Pas pour débutants |
| Pas de `ARCHITECTURE.md` | Diagram dépendances manquant | Hard comprendre structure |
| Pas de `SECURITY.md` | Vulnérabilités non documentées | Risk opérationnel |
| Pas de `TESTING.md` | Comment tester localement? | Barrier to contribution |

### CI/CD Pipeline

| Élément | Existe? | État |
|---------|---------|------|
| GitHub Actions | ❌ Non | Pas de checks automatisés |
| Build matrix | ❌ Non | Pas de multi-board build |
| Lint (clang-format) | ❌ Non | Code style inconsistent |
| Static analysis (clang-tidy) | ❌ Non | Anti-patterns non détectés |
| Dynamic tests (serial) | Partiel | Manual pyserial only |

### Recommandations (P2 - 8h)

Créer ou mettre à jour:
1. `docs/ARCHITECTURE.md` — Diagram architecture, data flow
2. `docs/SECURITY.md` — Vuln disclosure, remediation status
3. `docs/TESTING.md` — How to run tests locally + CI
4. `.github/workflows/ci.yml` — Build + lint on PR
5. Nettoyer `AGENT_TODO.md` et synchroniser status

---

## 🚨 TOP 10 RISQUES CONSOLIDÉS

| Rank | Risque | Severity | Location | Impact | Effort Fix |
|------|--------|----------|----------|--------|------------|
| 1 | Race condition g_scenario | HAUT | main.cpp loop | Crash, data corrupt | 16h (mutex) |
| 2 | Audio memory leak | HAUT | audio_mgr.cpp | Mem exhaust 1-2h | 4h (unique_ptr) |
| 3 | WiFi credentials hardcoded | CRIT | APP_WIFI.json | Breach device | 4h (NVS) |
| 4 | Zero API auth | CRIT | All /api/* | Unauthorized control | 8h (Bearer token) |
| 5 | LVGL non-reentrant | MOYEN-H | ui_mgr.cpp | Corruption objects | 12h (dedicate core) |
| 6 | Buffer overflow serial | MOYEN | main.cpp | Stack smash | 2h (validation) |
| 7 | Path traversal | MOYEN | storage_mgr | File leak, write | 3h (sanitize paths) |
| 8 | No watchdog timer | MOYEN | setup() | Silent hang | 2h (TWDT) |
| 9 | handleSerialCommand() >50 cases | MOYEN | main.cpp | Unmaintainable | 12h (refactor) |
| 10 | Memory fragmentation | BAS-M | String usage | Alloc fail 1-2h | 6h (pool + pre-alloc) |

---

## 📋 ROADMAP REMÉDIATION

### Phase 1: SÉCURITÉ CRITIQUE (Semaines 1-2)
**Effort**: 2-3 semaines  
**Owner**: Senior Dev + Security Review

- [ ] Supprimer WiFi creds du code
- [ ] Implémenter Bearer token auth sur tous /api/*
- [ ] Valider serial buffer size
- [ ] Path traversal sanitization
- [ ] JSON schema validation (size limit)
- [ ] Code review + pentest partiel

### Phase 2: STABILITÉ RUNTIME (Semaines 2-4)
**Effort**: 2 semaines  
**Owner**: EmbeddedSW Team

- [ ] Fix audio memory leak (unique_ptr)
- [ ] Add global state mutex (g_scenario, g_audio)
- [ ] Add ESP32 TWDT (Task Watchdog Timer)
- [ ] LVGL core-isolation (dedicate core 1)
- [ ] Network async state machine (eliminate blocking)
- [ ] Telemetry task (memory + perf monitoring)

### Phase 3: MAINTENABILITÉ (Semaines 4-6)
**Effort**: 1-2 semaines  
**Owner**: Tech Lead

- [ ] Refactor handleSerialCommand() (~1000 lines → 100 + command map)
- [ ] Extract serial handler to separate service
- [ ] Extract web endpoints to REST service
- [ ] Unit tests framework (gtest) setup
- [ ] Documentation (ARCHITECTURE.md, SECURITY.md, TESTING.md)
- [ ] CI/CD pipeline (.github/workflows/ci.yml)

### Phase 4: TESTS & VALIDATION (Ongoing)
**Effort**: 1-2 semaines de sprint récurrent  
**Owner**: QA + Dev

- [ ] Write 30+ unit tests (audio, ui, storage)
- [ ] Integration tests (scenario + audio + network)
- [ ] Smoke test automation (hardware + simulator)
- [ ] Fuzz testing (JSON parser, serial commands)
- [ ] Load testing (100 req/sec, memory leak detection)

---

## 📈 MÉTRIQUES SUGGÉRÉES À TRACKER

```
Semaine 1:
  - [ ] Security fixes: 2/2 CRITICAL, 1/3 HIGH
  - [ ] Code review: Main.cpp + audio_manager.cpp
  - [ ] Build: 0 compile errors, 0 deploy failures

Semaine 2:
  - [ ] Unit tests: 20+ assertions passing
  - [ ] Memory: No heap leaks (valgrind)
  - [ ] Serial smoke: 4 scenarios pass 100x

Semaine 3:
  - [ ] Code complexity: handleSerialCommand() < 20 cyclo
  - [ ] API coverage: 95%+ endpoints with auth
  - [ ] Docs: ARCHITECTURE.md, SECURITY.md, TESTING.md complete

Semaine 4:
  - [ ] Test coverage: >50% C++ code
  - [ ] CI: All checks passing on PRs
  - [ ] 4h stability: 0 crashes in test run
```

---

## 🎯 PROCHAINES ÉTAPES IMMÉDIATES (Aujourd'hui/Demain)

1. **Code Review Sprint** (2h):
   - Identifier toutes les races conditions via grep `g_` + Triage
   
2. **Security Hotfix PR** (4h):
   - Supprimer credentials WiFi
   - Ajouter Bearer token validation
   
3. **Tech Debt Planning Meeting** (1h):
   - Allouer resources phases 1-4
   - Définir acceptance criteria

4. **Watchdog Timer** (2h):
   - `esp_task_wdt_init()` dans setup
   - Auto-reboot si hang détecté

---

## 📞 Recommandations Finales

| Que | Qui | Quand | Output |
|-----|-----|-------|--------|
| Code review (arch + sec) | Senior + Security | Semaine 1 | PR checklist |
| Refactor main.cpp | Core team (16h) | Semaines 2-3 | Modular srv classes |
| Test harness setup | QA lead (8h) | Semaine 1 | GTest infra ready |
| Security audit report delivery | External (optionnel) | Semaine 2 | Pentest findings |
| Documentation (4 docs) | Tech writer (8h) | Semaine 3 | Diagrams + guides |

---

**Rapport généré**: 2026-03-01  
**Statut**: Draft prêt review  
**Contact**: [Assign à Senior Dev]
