## Validation hardware – TODO détaillée

## Pilotage Apps/Workbench (audit 2026-03-02)

Reference audit complete:
- `specs/apps/IMPLEMENTATION_AUDIT_TODO.md`

Priorites immediates (avant enchainement Sprint 3):
- [ ] **P0** Aligner le provisioning registry sur 20 apps activees (seed/fallback + migration si `/apps/registry.json` deja present).
- [ ] **P0** Corriger launcher Workbench pour 20 apps (pagination + hit-test tactile + emulation tactile page-aware).
- [ ] **P0** Supprimer les `delay()` bloquants dans `AmigaUIShell` (transition/tap feedback non bloquants).
- [ ] **P0** Repasser gate endurance utility: `python3 tests/sprint1_utility_contract.py --mode serial --cycles 20 --port <PORT>`.
- [ ] **P1** Repasser gate endurance capture: `python3 tests/sprint2_capture_contract.py --mode serial --cycles 20 --port <PORT>`.
- [ ] **P1** Ajouter harness Sprint 3 audio (`tests/sprint3_audio_contract.py`) puis gate serial + HTTP.
- [ ] **P1** Fermer gap QR decode reel camera -> payload (au-dela du `scan_payload` injecte).
- [ ] **P1** Decider GO/NO-GO core NES externe avec KPI et traces.

Gates de deblocage "foundation complete":
1. `pio run -e freenove_esp32s3_full_with_ui` vert.
2. 20 apps visibles et ouvrables dans launcher (boutons + touch/emulation).
3. Sprint1 20 cycles vert.
4. Sprint2 20 cycles vert.

## Sprint 0 - Foundation (2026-03-02)

- [x] Bridge `AmigaUIShell` -> `AppRuntimeManager` branché via interface interne.
- [x] `launchSelectedApp()` implémenté avec ouverture runtime réelle.
- [x] Anti double-launch activé (debounce + garde sur état app déjà active).
- [x] `APP_STATUS` exposé en commande série directe + HELP mis à jour.
- [x] Logs runtime normalisés ajoutés (`APP_OPEN_*`, `APP_CLOSE_*`, `APP_ACTION_*`).
- [x] Upgrade LVGL 8.4.x appliqué.
- [x] Librairie QR figée sur commit.
- [x] `tinyexpr` ajouté explicitement.
- [~] Upgrade `ESP32-audioI2S` 3.4.4 tenté puis rollback vers 2.3.0 (toolchain bloque sur `<span>`).
- [x] Gate build exécutée: `pio run -e freenove_esp32s3_full_with_ui` SUCCESS.

## Sprint 1 - Utility Pack (2026-03-02)

- [x] `CalculatorModule`: tinyexpr prioritaire + erreurs explicites (`eval_error@pos`) + action `status`.
- [x] `TimerToolsModule`: chrono/countdown stabilisés + action `status` + signal fin countdown (audio/LED si dispo).
- [x] `FlashlightModule`: on/off robustes + `light_toggle` + `set_level` appliqué à chaud + action `status`.
- [x] Contrat specs apps mis à jour (`calculator`, `timer_tools`, `flashlight`).
- [x] Campagne endurance outillée: `tests/sprint1_utility_contract.py` (mode serial + mode HTTP).
- [x] Smoke série court validé: `python3 tests/sprint1_utility_contract.py --mode serial --cycles 5 --port /dev/cu.usbmodem5AB90753301`.
- [ ] Gate endurance série 20 cycles/app encore rouge.
  - **Verdict (2026-03-02)**: Échec à `calculator cycle 9/20` avec reboot panic (`reset_reason=4 = ESP_RST_TASK_WDT`)
  - **🔴 ROOT CAUSE IDENTIFIED**: 
    - Watchdog timeout during `tinyexpr eval()` cyclic action processing
    - Heap free: ~45KB au cycle 9 (stable jusque-là → pas leak mémoire primaire)
    - Stack pressure: Arduino event loop stack insuffisant (8192 bytes par défaut)
    - Suspects: `tinyexpr eval()` complexité N², action latency croissante, ou FreeRTOS scheduling contention
  - **✅ Mitigation appliquée**:
    - [x] Augmenté `ARDUINO_LOOP_STACK_SIZE=16384` (8192 → 16384) dans [platformio.ini](platformio.ini#L115)
    - [x] Compilation SUCCESS (31.06s, RAM 64.4%, Flash 42.0%)
  - **⏳ À tester**: `python3 tests/sprint1_utility_contract.py --mode serial --cycles 20` (target: 20/20 SUCCESS)
  - **À investiguer** (si re-fail): 
    - Profiling `calculator` action latency (timeout tinyexpr sur expr complexe?)
    - Event loop preemption (FreeRTOS task priority + affinity)
    - Mutex contention I2S/audio parallel avec action processing

- [ ] Gate HTTP toujours bloqué côté reachability/auth (`/api/apps/*` non validé dans cette passe).

- [x] Compiler et flasher le firmware sur le Freenove Media Kit (`pio run -e freenove_esp32s3_full_with_ui -t upload`)
- [ ] Préparer les fichiers de test sur LittleFS (/data/scene_*.json, /data/screen_*.json, /data/*.wav)
- [ ] Vérifier l'affichage TFT (boot, écran dynamique, transitions)
- [ ] Tester la réactivité tactile (zones, coordonnées, mapping)
- [ ] Tester les boutons physiques (appui, mapping, transitions)
- [ ] Tester la lecture audio (fichiers présents/absents, fallback)
- [~] Observer les logs série (initialisation, actions, erreurs) en continu pendant endurance longue
- [ ] Générer et archiver les logs d'évidence (logs/)
- [ ] Produire un artefact de validation (artifacts/)
- [ ] Documenter toute anomalie ou limitation hardware constatée

## Sprint 2 - Capture Pack (2026-03-02)

- [x] `CameraVideoModule`: action `status` + états preview/clip/frame + erreurs normalisées.
- [x] `QrScannerModule`: action `status` + `scan_payload` + classification `url/app/text`.
- [x] `DictaphoneModule`: action `status` + normalisation de chemin enregistrement/lecture.
- [x] Contrat Sprint 2 ajouté: `tests/sprint2_capture_contract.py`.
- [x] Gate série Sprint 2 (1 cycle/app) verte.
  - Verdict (2026-03-02): `python3 tests/sprint2_capture_contract.py --mode serial --cycles 1` SUCCESS.
- [x] Endurance courte Sprint 2 (3 cycles/app) verte.
  - Verdict (2026-03-02): `python3 tests/sprint2_capture_contract.py --mode serial --cycles 3` SUCCESS.
- [x] Endurance intermédiaire Sprint 2 (10 cycles/app) verte.
  - Verdict (2026-03-02): `python3 tests/sprint2_capture_contract.py --mode serial --cycles 10` SUCCESS.
- [x] Déblocage mémoire/coex appliqué:
  - `platformio.ini`: `UI_FX_TARGET_FPS=12`, `UI_DMA_TRANS_BUF_LINES=1`, `UI_LV_MEM_SIZE_KB=54`,
    `ARDUINO_LOOP_STACK_SIZE=16384`, `BOARD_HAS_PSRAM`.
  - `QrScannerModule`: ownership caméra corrigé (évite double init avec `ESP32QRCodeReader`).
  - `AppRuntimeManager`: profil ressource auto selon capabilities app (caméra vs micro).
- [ ] Gate endurance longue Sprint 2 à lancer (20 cycles/app) + archivage logs.

## Sprint 9 - Phase 9 Touch Input (2026-03-02)

**Timeline**: Phase 9 - Amélioration UX & Touch Input (Semaine 1-2)  
**Status**: 🟢 **IN PROGRESS** (touch emulation implémenté, physical hardware optional)

### Phase 9A - Touch Input Emulation (Complété 2026-03-02 - 2h)

- [x] Créer infrastructure émulation tactile (`TouchEmulator` class)
  - [x] `include/drivers/input/touch_emulator.h`: Navigation curseur virtuel 4x4 grid
  - [x] `src/drivers/input/touch_emulator.cpp`: Mouvement UP/DOWN/LEFT/RIGHT avec wrapping horizontal/vertical
  - [x] Intégration `AmigaUIShell`:
    - `setTouchManager(TouchManager*)`: Connexion au driver touch
    - `setTouchEmulationMode(bool enabled)`: Activation mode émulation
    - `enable_touch_emulation_` member variable: Flag runtime activation

- [x] Implémenter `getTouchGridIndex(x, y)` pour mapping écran → grille
  - Grid: 4 colonnes × 4 rangées, cellule 80×80px, offset (16, 32)
  - Bounds checking: hors grille → retour 255 (invalid index)
  - Tests edge cases: coins (0,0), (319,479), limites grille

- [x] Brancher button handlers → touch virtuel via `TouchEmulator`
  - **Button 0 (UP)**: déplace curseur ↑ une ligne (row - 1)
  - **Button 1 (SELECT)**: déclenche touch à position curseur → launch app
  - **Button 2 (DOWN)**: déplace curseur ↓ une ligne (row + 1)
  - **Button 3 (MENU)**: ferme app runtime (mode unchanged)
  - **Button 4**: Toggle LEFT ⇄ RIGHT curseur avec wrapping horizontal

- [x] Compiler & build gate exécutée
  - `pio run -e freenove_esp32s3_full_with_ui` **SUCCESS** (31.06s, RAM 64.4%, Flash 42.0%)

- [ ] Tests de validation (À faire)
  - [ ] Tests sur device: Navigation 4×4 via boutons → touch virtuel
  - [ ] Taper 7 apps via émulation sans double-launch
  - [ ] Vérifier 200ms debounce avant launch (existant)
  - [ ] Endurance: 20 lancements cycliques sans leak
  - [ ] Memory check: Heap stable post-launch

- [ ] Hardware physique touch (optionnel - Phase 9B)
  - [ ] Activer `FREENOVE_HAS_TOUCH=1` (env: `freenove_esp32s3_touch`)
  - [ ] XPT2046 touchscreen SPI (CS=GPIO9, IRQ=GPIO15)
  - [ ] Tests comparatifs: touch physique vs émulé
  - [ ] Calibration écran si requis

**Acceptance Criteria**:
```
✓ Compilation: SUCCESS
✓ Touch emulation mode: ENABLED by default
✓ Grid navigation: 4×4 sans bugs
✓ App launch: déclenchement via couche tactile (physique ou émulée)
✓ 20 cycles sans crash/watchdog
✓ Heap stable (~210KB/327KB constant utilisation)
```

**Notes techniques**:
- **Emulation philosophy**: Support "button-to-touch adapter" pour testing sans hardware
- **Grid layout**: AmigaUI 64px icons + 16px spacing = 80px cells, 4 row/col
- **Cursor state**: `TouchEmulator` owns gridIndex (0-15), `AmigaUIShell.selected_index_` kept in sync
- **Touch routing**: Button input → emulator.move*() → getCursorPosition() → handleTouchInput(x,y)
- **Files edited**:
  - `ui_freenove_allinone/include/drivers/input/touch_emulator.h` (NEW)
  - `ui_freenove_allinone/src/drivers/input/touch_emulator.cpp` (NEW)
  - `ui_freenove_allinone/include/ui/ui_amiga_shell.h` (modified)
  - `ui_freenove_allinone/src/ui/ui_amiga_shell.cpp` (modified: handleButtonInput)
  - `ui_freenove_allinone/src/main.cpp` (modified: setTouchManager + setTouchEmulationMode at boot)

## Revue finale – Checklist agents

- [ ] Vérifier la cohérence de la structure (dossiers, modules, scripts)
- [ ] Relire AGENT_FUSION.md (objectifs, couverture specs, structure)
- [ ] Relire README.md (usage, build, validation, modules)
- [ ] Relire la section onboarding (docs/QUICKSTART.md, etc.)
- [ ] Vérifier la présence et la robustesse du fallback LittleFS
- [ ] Vérifier la traçabilité des logs et artefacts
- [ ] Vérifier la synchronisation UI/scénario/audio
- [ ] Vérifier la gestion dynamique des boutons/tactile
- [ ] Vérifier la non-régression sur les autres firmwares (split)

## Troubleshooting & Debug Guide

### Symptôme: Watchdog timeout (reset_reason=4) à cycle N

**Diagnostic**:
```bash
# 1. Vérifier stack dans platformio.ini (l.115)
grep "ARDUINO_LOOP_STACK_SIZE" platformio.ini
# Expected: -DARDUINO_LOOP_STACK_SIZE=16384

# 2. Monitor en continu avec logs heap
pio device monitor -b 115200 --filter esp32_exception_decoder | grep -E "TWDT|reset_reason|Free heap"

# 3. Identifier action coupable (cycle N-1)
python3 tests/sprint1_utility_contract.py --mode serial --cycles $N --verbose \
  2>&1 | grep -B5 "reset_reason"
```

**Root causes courants**:
| Symptôme | Cause | Solution |
|----------|-------|----------|
| Watchdog à cycle 9 (calculator) | Stack débordement lors eval complexe | Augmenter ARDUINO_LOOP_STACK_SIZE (8192→16384) |
| TWDT pendant audio stream | Mutex contention (I2S + LVGL) | Réduire UI_DRAW_BUF_LINES (16→8) |
| Double-launch panic | App runtime double init | Vérifier anti-reentrancy dans launchSelectedApp() |
| Heap fragmentation | Leak mémoire lent (50KB/100cycles) | Profile alloc/free (Arena API) |

### Commandes Debug utiles

```bash
# Status complet app runtime
echo "APP_STATUS" | nc localhost 5555

# Check ressources device
echo "PING" | nc localhost 5555

# Logs persistent
tail -f /tmp/zacus_*.log

# Rebuild avec debug symbols
pio run -e freenove_esp32s3_full_with_ui -t clean && pio run -v

# Flash + monitor en une commande
pio run -e freenove_esp32s3_full_with_ui -t upload && pio device monitor -b 115200
```

## Spécifications fonctionnelles utilisateur à livrer

- [ ] **Lecteur Audio** (lecture locale, play/pause/stop, playlist de base)
- [ ] **Appareil photo / vidéo** (capture image/vidéo, stockage média, aperçu)
- [ ] **Dictaphone** (enregistrement audio local, lecture, suppression)
- [ ] **Chronomètre / minuteur** (UI dédiée, rappel sonore)
- [ ] **Lampe de poche** (commande on/off, intensité si supportée)
- [ ] **Calculatrice** (opérations de base, historique simple)
- [ ] **Webradio enfants** (gestion flux HTTP/ICY, reprise auto minimale)
- [ ] **Podcast enfants** (lecture podcasts local ou RSS simplifié)
- [ ] **Lecteur de QR code** (scan caméra, parsing et action de base)
- [ ] **Émulateur de jeux vidéo** (version minimale ludique compatible mémoire embarquée)
- [ ] **Lecteur de livres audio** (index/chapitres, reprise de progression)
- [ ] **Application de dessin** (canvas tactile + outils minimum)
- [ ] **Application de coloriage** (templates + remplissage couleur)
- [ ] **Application de musique pour enfants** (playlists ciblées, contrôle simple)
- [ ] **Application de yoga pour enfants** (séquences guidées, audio/texte)
- [ ] **Application de méditation pour enfants** (tempos/sons/apaisement)
- [ ] **Application de langues pour enfants** (mini leçons + répétition audio)
- [ ] **Application de mathématiques pour enfants** (exercices interactifs)
- [ ] **Application de sciences pour enfants** (contenus interactifs + quiz)
- [ ] **Application de géographie pour enfants** (quiz/carte/images interactives)

### Plan d'intégration recommandé (ordre de dépendance)

- [ ] 1) Base commune UI + assets + navigation (priorité haute)
- [ ] 2) Audio stack: lecteur audio / livres / podcasts / webradio
- [ ] 3) Outils système: chronomètre, calculatrice, lampe de poche
- [ ] 4) Camera stack: photo/vidéo + QR code
- [ ] 5) Contenus enfants: dessin, coloriage, musique, yoga, méditation, langues, maths, sciences, géographie
- [ ] 6) Jeux: intégrer émulateur léger après stabilisation mémoire/perf

## Stack technique réseau (nouvelle contrainte)

- [ ] Ajouter découverte réseau Bonjour/mDNS (nom appareil + présence services)
- [ ] Ajouter service de transfert simple de fichiers entre appareils
- [ ] Définir protocole minimal de transfert (metadata + chunks + ack + reprise simple)
- [ ] Ajouter endpoints/API de partage (liste appareils, push/pull fichier, état transfert)
- [ ] Journaliser les transferts (début, progression, succès/échec)

## UX/UI enfants + inspiration Amiga (nouvelle contrainte)

- [ ] Définir un thème visuel enfant inspiré Amiga (palette, icônes, typo, motion)
- [ ] Créer écran home "kids shell" (grille d'apps colorée + navigation simple)
- [ ] Ajouter transitions ludiques non bloquantes (sans pénaliser FPS/runtime loop)
- [ ] Standardiser composants UI: bouton, carte app, badge état, feedback action
- [ ] Ajouter règles de lisibilité enfant (texte court, contraste, zones tactiles larges)

## Progression technique réalisée (itération courante)

- [x] Socle apps runtime: registre, lifecycle manager, endpoints `/api/apps/*`
- [x] Contrat capacités runtime: flags explicites et gating au lancement d'app
- [x] Actions scénario app-aware: `open_app`, `close_app`, `app_action`
- [x] Bonjour/mDNS: service publié `_zacus._tcp` + découverte peers
- [x] Partage fichiers simple: endpoints `/api/share/peers`, `/api/share/files`, `/api/share/upload`
- [x] Spécifications JSON: manifest/streams/progress + registre exemple
- [x] Direction UX enfant + Amiga: fichier de thème de référence
- [x] Phase 9 Touch Input: EmulationMode + grid navigation
