# TODO - ESP32_ZACUS Implementation Plan
**Date de création**: 2 Mars 2026  
**Statut global**: 🟢 PRÊT POUR PRODUCTION avec améliorations  
**Référence**: CODE_AUDIT_ET_PLAN_ACTION_2026-03-02.md

---

## 🔴 PRIORITÉ P1 - URGENT (0-7 jours)

### P1.1 - Phase 9 Touch Input Implementation ⏱️ 16h
**Deadline**: 4 Mars 2026  
**Owner**: Dev Embedded  
**Status**: 🔵 NOT STARTED

#### Tâches détaillées:

- [ ] **getTouchGridIndex() implementation** (4h)
  - [ ] Ouvrir [ui_freenove_allinone/src/ui/ui_amiga_shell.cpp](ui_freenove_allinone/src/ui/ui_amiga_shell.cpp)
  - [ ] Implémenter calcul grid coordinates
    ```cpp
    uint8_t AmigaUIShell::getTouchGridIndex(uint16_t x, uint16_t y) {
      uint8_t col = x / (ICON_SIZE + ICON_SPACING);  // 64 + 16 = 80px
      uint8_t row = y / (ICON_SIZE + ICON_SPACING);
      if (col >= GRID_COLS || row >= GRID_ROWS) return 255;
      uint8_t index = row * GRID_COLS + col;
      return (index < 7) ? index : 255;
    }
    ```
  - [ ] Ajouter validation bounds (grid_index < 7)
  - [ ] Tests edge cases (coins, hors grille)
  - [ ] Compiler: `pio run -e freenove_esp32s3_full_with_ui`

- [ ] **launchSelectedApp() completion** (6h)
  - [ ] Compléter logique launch dans [ui_amiga_shell.cpp](ui_freenove_allinone/src/ui/ui_amiga_shell.cpp#L100)
    ```cpp
    void AmigaUIShell::launchSelectedApp() {
      if (selected_index_ >= 7) return;
      const AppIcon& app = APPS[selected_index_];
      
      // Anti double-launch
      if (g_app_runtime.isAppActive()) {
        Serial.println("[SHELL] App already running, ignoring");
        return;
      }
      
      // Launch
      bool success = g_app_runtime.openApp(app.app_id);
      if (success) {
        Serial.printf("[SHELL] Launched: %s\n", app.app_id);
      } else {
        Serial.printf("[SHELL] ERROR: Failed to launch %s\n", app.app_id);
      }
    }
    ```
  - [ ] Intégrer visual feedback (pulse animation)
  - [ ] Error handling app already running
  - [ ] Tests launch/close 10 cycles

- [ ] **Button handlers integration** (4h)
  - [ ] Implémenter navigation UP/DOWN dans [main.cpp](ui_freenove_allinone/src/main.cpp)
  - [ ] Button SELECT → launchSelectedApp()
  - [ ] Button MENU → return to launcher
  - [ ] Debounce 50ms
  - [ ] Tests navigation complète

- [ ] **Validation tests** (2h)
  - [ ] Touch test: taper 7 positions grille
  - [ ] Button test: naviguer avec UP/DOWN/SELECT
  - [ ] Endurance: 20 launches sans leak
  - [ ] Memory check: heap stable après 20 cycles
  - [ ] Documenter résultats dans logs/

**Acceptance Criteria**:
```
✓ Touch (0,0) → app[0] launches
✓ Button SELECT → app launches
✓ Button MENU → return launcher
✓ 20 launches → no memory leak
✓ Visual feedback OK
```

---

### P1.2 - Mise à jour Documentation AGENT_TODO.md ⏱️ 4h
**Deadline**: 3 Mars 2026  
**Owner**: Tech Lead  
**Status**: 🔵 NOT STARTED

#### Tâches détaillées:

- [ ] **Update Sprint 1 status** (2h)
  - [ ] Ouvrir [ui_freenove_allinone/AGENT_TODO.md](ui_freenove_allinone/AGENT_TODO.md)
  - [ ] Documenter gate rouge:
    ```markdown
    - [x] Gate endurance série 20 cycles/app: ⚠️ ROUGE
      - Verdict: échec cycle 9/20 avec reset_reason=4 (watchdog)
      - Root cause: Stack Arduino loop insuffisant (8192 → 16384 requis)
      - Mitigation: ARDUINO_LOOP_STACK_SIZE=16384 dans platformio.ini
      - Re-test requis: 20 cycles complets
    ```
  - [ ] Ajouter memory pressure analysis
  - [ ] Documenter mitigation steps

- [ ] **Update Sprint 2 status** (0.5h)
  - [ ] Marquer gate 10 cycles verte
  - [ ] Documenter déblocage coex/mémoire
  - [ ] Ajouter metrics (heap libre, etc.)

- [ ] **Phase 9 checklist refresh** (1h)
  - [ ] Touch input → IN PROGRESS
  - [ ] App launch → READY
  - [ ] Sync avec [PHASE9_PLAN.md](PHASE9_PLAN.md)
  - [ ] Update task IDs

- [ ] **Troubleshooting section** (0.5h)
  - [ ] Reboot panic handling guide
  - [ ] Serial command debugging tips
  - [ ] Memory check commands
  - [ ] Common issues FAQ

**Acceptance Criteria**:
```
✓ AGENT_TODO.md reflects reality
✓ Sprint 1 failure explained
✓ Phase 9 aligned with plan
✓ Troubleshooting guide added
```

---

### P1.3 - Tests Endurance Sprint 1 Fix ⏱️ 8h
**Deadline**: 4 Mars 2026  
**Owner**: Dev Embedded + QA  
**Status**: 🔵 NOT STARTED

#### Tâches détaillées:

- [ ] **Analyser logs panic** (2h)
  - [ ] Run test avec logging:
    ```bash
    python3 tests/sprint1_utility_contract.py \
      --mode serial --cycles 20 --verbose \
      2>&1 | tee logs/sprint1_debug_$(date +%Y%m%d_%H%M%S).log
    ```
  - [ ] Extraire stack traces reset_reason=4
  - [ ] Analyser heap usage pre-panic
  - [ ] Identifier mutex timeout suspects

- [ ] **Stack size verification** (1h)
  - [ ] Vérifier [platformio.ini](platformio.ini) actuel
  - [ ] Confirmer `ARDUINO_LOOP_STACK_SIZE=16384`
  - [ ] Si absent, ajouter dans build_flags:
    ```ini
    build_flags =
      ...
      -DARDUINO_LOOP_STACK_SIZE=16384
    ```

- [ ] **Memory pressure reduction** (3h)
  - [ ] CalculatorModule: vérifier String allocations dans eval
  - [ ] TimerToolsModule: pre-allocate countdown buffers
  - [ ] FlashlightModule: PWM sans heap usage
  - [ ] Ajouter logs heap avant/après chaque cycle

- [ ] **Re-run endurance** (2h)
  - [ ] Target: 20/20 cycles SUCCESS
  - [ ] Monitor heap libre > 50KB
  - [ ] Zero watchdog timeouts
  - [ ] Logs clean (pas mutex errors)
  - [ ] Archive logs dans logs/sprint1_success/

**Acceptance Criteria**:
```
✓ python3 tests/sprint1_utility_contract.py --cycles 20 → SUCCESS
✓ Heap libre > 50KB après 20 cycles
✓ Zero watchdog reboots
✓ Logs clean
```

---

## 🟡 PRIORITÉ P2 - IMPORTANT (7-21 jours)

### P2.1 - Refactoring main.cpp ⏱️ 24h
**Deadline**: 11 Mars 2026  
**Owner**: Senior Dev  
**Status**: 🔵 NOT STARTED

#### Tâches:

- [ ] **Extraire SerialCommandService** (10h)
  - [ ] Créer [ui_freenove_allinone/include/runtime/serial_command_service.h](ui_freenove_allinone/include/runtime/serial_command_service.h)
  - [ ] Créer [ui_freenove_allinone/src/runtime/serial_command_service.cpp](ui_freenove_allinone/src/runtime/serial_command_service.cpp)
  - [ ] Migrer handleSerialCommand() →50 cases
  - [ ] Command map avec function pointers
  - [ ] Tests: tous commands fonctionnels

- [ ] **Extraire WebApiService** (10h)
  - [ ] Créer web_api_service.h/cpp
  - [ ] Migrer tous `/api/*` handlers
  - [ ] Bearer token validation centralisée
  - [ ] Tests: endpoints répondent

- [ ] **LoopCoordinator** (4h)
  - [ ] Encapsuler loop() logic
  - [ ] Watchdog feeding
  - [ ] Telemetry collection
  - [ ] Tests: loop cycle time stable

**Target**: main.cpp 3876 → <1500 lignes

---

### P2.2 - Unit Tests C++ Framework ⏱️ 16h
**Deadline**: 14 Mars 2026  
**Owner**: QA Lead  
**Status**: 🔵 NOT STARTED

#### Tâches:

- [ ] **GoogleTest setup** (4h)
  - [ ] Ajouter dépendance platformio.ini
  - [ ] Config test environment
  - [ ] Exemple test basique
  - [ ] CI integration

- [ ] **Write 20 unit tests** (10h)
  - [ ] AudioManager tests (6)
  - [ ] StorageManager tests (4)
  - [ ] ButtonManager tests (4)
  - [ ] WiFiConfig tests (6)

- [ ] **Documentation** (2h)
  - [ ] Créer TESTING.md
  - [ ] Guidelines tests
  - [ ] CI process

**Target**: Coverage ≥30%, `pio test` passing

---

### P2.3 - Sécurité P2 (Buffer & Path) ⏱️ 8h
**Deadline**: 7 Mars 2026  
**Owner**: Security + Dev  
**Status**: 🔵 NOT STARTED

#### Tâches:

- [ ] **Buffer overflow fix** (2h)
  - [ ] Limiter serial input 128 bytes
  - [ ] Validation stricte
  - [ ] Tests overflow attempts

- [ ] **Path traversal fix** (3h)
  - [ ] Whitelist paths: /data/, /music/, /story/
  - [ ] Reject ../ patterns
  - [ ] Normalize paths

- [ ] **JSON robustness** (3h)
  - [ ] Try/catch deserialize
  - [ ] Max size 12KB enforcement
  - [ ] Error handling

**Target**: Pentest 10/10 scénarios bloqués

---

## 🔵 PRIORITÉ P3 - NICE TO HAVE (>21 jours)

### P3.1 - Telemetry & Monitoring ⏱️ 8h
- [ ] FreeRTOS telemetry task
- [ ] Heap usage logging
- [ ] LVGL memory stats
- [ ] Network RSSI monitoring

### P3.2 - Documentation Architecture ⏱️ 16h
- [ ] Créer ARCHITECTURE.md avec diagrammes
- [ ] API_REFERENCE.md endpoints
- [ ] SECURITY.md disclosure
- [ ] Contribution guide

### P3.3 - String Fragmentation Fix ⏱️ 8h
- [ ] Audit tous String.append()
- [ ] Remplacer par char[] buffers
- [ ] String pool messages
- [ ] Test 4h runtime

### P3.4 - CI/CD Pipeline ⏱️ 16h
- [ ] GitHub Actions multi-board
- [ ] Clang-format lint
- [ ] Clang-tidy analysis
- [ ] Auto-deploy artifacts

---

## 📊 SUIVI PROGRESSION

### Semaine 1 (2-8 Mars)
- [ ] Phase 9 touch input FONCTIONNEL
- [ ] AGENT_TODO.md À JOUR
- [ ] Sprint 1 gate VERTE (20/20)
- [ ] Security P2 COMPLÉTÉ

**Burndown**: 28h P1

### Semaine 2 (9-15 Mars)
- [ ] main.cpp REFACTORÉ
- [ ] Unit tests 20+ PASSING
- [ ] ARCHITECTURE.md créé

**Burndown**: 40h P2

### Semaine 3 (16-22 Mars)
- [ ] CI/CD ACTIF
- [ ] Coverage >50%
- [ ] Release v1.0-rc1 TAGGÉ

**Burndown**: 24h P2 + P3

---

## 🎯 ACTIONS IMMÉDIATES (Aujourd'hui)

### 1️⃣ Phase 9 Touch - Démarrer (30min)
```bash
cd /Users/cils/Documents/Lelectron_rare/ESP32_ZACUS
code ui_freenove_allinone/src/ui/ui_amiga_shell.cpp
# Implémenter getTouchGridIndex()
```

### 2️⃣ AGENT_TODO sync (15min)
```bash
code ui_freenove_allinone/AGENT_TODO.md
# Update Sprint 1 status
```

### 3️⃣ Sprint 1 debug run (1h)
```bash
python3 tests/sprint1_utility_contract.py \
  --mode serial --cycles 5 --verbose
# Analyser premiers résultats
```

---

## 📈 MÉTRIQUES CLÉS

### Code Quality
- `main.cpp`: 3876 lignes → Target: <1500
- Complexité: handleSerialCommand() ~50 → Target: <10
- Test coverage: ~15% → Target: >50%

### Stability
- Sprint 1 gate: 9/20 → Target: 20/20
- Sprint 2 gate: 10/10 → ✅ GOOD
- Memory leaks: 0 → ✅ GOOD
- Watchdog reboots: Occasionnels → Target: 0

### Security
- P0 vulns: 0/2 → ✅ FIXED
- P1 vulns: 0/3 → ✅ FIXED
- P2 vulns: 3/3 → Target: 0/3
- API auth coverage: 100% → ✅ GOOD

---

## 📞 RESSOURCES NÉCESSAIRES

### Team
- Dev Embedded: 40h (Phase 9 + Sprint 1 fix)
- Senior Dev: 24h (Refactoring)
- QA Lead: 20h (Tests framework)
- Security: 8h (P2 fixes)

### Timeline
- Semaine 1: P1 completion
- Semaine 2-3: P2 implementation
- Semaine 4: P3 + Release

### Budget Total
- P1: 28h (URGENT)
- P2: 48h (IMPORTANT)
- P3: 48h (NICE TO HAVE)
- **Total**: ~124h (~3 semaines)

---

**Dernière màj**: 2 Mars 2026  
**Prochaine revue**: 9 Mars 2026  
**Owner**: Dev Team Lead
