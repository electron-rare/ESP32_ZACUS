# Plan Sprint Applications Zacus

Date de reference: 2026-03-02
Scope: 20 apps declarees dans `data/apps/registry.json`

## 1. Strategie de priorisation

Principe:
- Priorite P0: tout ce qui bloque le parcours utilisateur "launcher -> app -> retour launcher".
- Priorite P1: apps coeur (utility, capture, audio) utilisables en conditions reelles.
- Priorite P2: apps kids et NES, une fois la plateforme stable.

Ordre d'execution:
1. Platforme runtime/launcher
2. Packs apps par module (pour mutualiser le code)
3. Hardening, perf, et validation hardware

## 2. Macro planning (6 sprints)

Hypothese charge: 1 dev principal, 5 jours/sprint.

### Sprint 0 (Semaine 1) - Fondations P0

Objectif:
- Rendre operationnel le flux complet de lancement d'app depuis `AmigaUIShell`.

Progression (2026-03-02):
- [x] Bridge launcher/runtime implemente.
- [x] `launchSelectedApp()` relie au runtime apps.
- [x] Anti double-launch (debounce + garde etat running/starting).
- [x] `APP_STATUS` serie expose + HELP mis a jour.
- [x] Logs normalises `APP_OPEN_*`, `APP_CLOSE_*`, `APP_ACTION_*`.
- [x] LVGL passe en 8.4.x.
- [x] QR lib pinnee sur commit.
- [x] tinyexpr ajoute explicitement.
- [~] `ESP32-audioI2S` 3.4.4 teste puis rollback en 2.3.0 (incompatibilite `<span>` avec toolchain actuelle).
- [x] Gate build `pio run -e freenove_esp32s3_full_with_ui` verte.

Scope:
- `ui_amiga_shell.cpp`: brancher `launchSelectedApp()` vers `APP_OPEN` (ou appel direct runtime manager).
- Navigation launcher: touch + boutons + prevention double launch.
- Retour propre vers `SCENE_READY` apres `APP_CLOSE`.
- Normalisation logs: `APP_OPEN_OK/FAIL`, `APP_CLOSE_OK/FAIL`.

Effort estime:
- 3 a 4 jh

Risque:
- Eleve (couplage UI/runtime actuel)

Gate sortie:
- `APP_OPEN audio_player` depuis launcher fonctionne.
- `APP_CLOSE` ramene sur launcher sans freeze.
- Build `freenove_esp32s3_full_with_ui` OK.

### Sprint 1 (Semaine 2) - Utility Pack P1

Apps:
- `calculator`
- `timer_tools`
- `flashlight`

Objectif:
- Finaliser les apps les moins risquées pour valider le pipeline UX.

Progression (2026-03-02):
- [x] Calculator: tinyexpr actif + erreurs explicites (`eval_error@pos`) + action `status`.
- [x] Timer: actions chrono/countdown stabilisées + action `status`.
- [x] Timer: signal fin countdown implémenté (audio/LED opportuniste si ressources disponibles).
- [x] Flashlight: `light_on/off` robustes + `light_toggle` + `set_level` appliqué à chaud.
- [x] Script d'endurance et de contrat serial/API ajouté: `tests/sprint1_utility_contract.py`.
- [x] Build + flash validés sur cible: `pio run -e freenove_esp32s3_full_with_ui` puis `-t upload`.
- [x] Smoke série court validé: `--cycles 5` vert (3 apps).
- [ ] Gate hardware 20 cycles/app à exécuter et archiver (logs + verdict).
  - Essai 2026-03-02: échec à `calculator cycle 9/20` avec reboot panic (`reset_reason=4`).
- [ ] Gate HTTP `/api/apps/*` à valider (reachability/auth encore instable côté poste de test).

Effort estime:
- calculator: 1 jh
- timer_tools: 1.5 jh
- flashlight: 1 jh
- integration/tests: 1 jh
- Total: 4.5 jh

Risque:
- Faible a moyen

Gate sortie:
- 3 apps stables sur 20 cycles open/close.
- Aucune erreur runtime persistante (`last_error` vide en nominal).

### Sprint 2 (Semaine 3) - Capture Pack P1

Apps:
- `camera_video`
- `qr_scanner`
- `dictaphone`

Objectif:
- Stabiliser camera/micro/filesystem en usage reel.

Progression (2026-03-02):
- [x] `CameraVideoModule`: action `status` + événements normalisés preview/clip/frame.
- [x] `QrScannerModule`: action `status` + alias `scan_payload` + classification `url/app/text`.
- [x] `DictaphoneModule`: action `status` + normalisation chemins relatifs (`/recorder/...`).
- [x] Harness Sprint 2 ajouté: `tests/sprint2_capture_contract.py`.
- [x] Gate série Sprint 2 (1 cycle/app) verte.
  - `python3 tests/sprint2_capture_contract.py --mode serial --cycles 1` -> SUCCESS.
- [x] Endurance courte Sprint 2 (3 cycles/app) verte.
  - `python3 tests/sprint2_capture_contract.py --mode serial --cycles 3` -> SUCCESS.
- [x] Endurance intermédiaire Sprint 2 (10 cycles/app) verte.
  - `python3 tests/sprint2_capture_contract.py --mode serial --cycles 10` -> SUCCESS.
- [x] Déblocage mémoire/coex validé:
  - `BOARD_HAS_PSRAM` activé (`psram_found=1` au boot).
  - tuning runtime: `UI_FX_TARGET_FPS=12`, `UI_DMA_TRANS_BUF_LINES=1`, `UI_LV_MEM_SIZE_KB=54`,
    `ARDUINO_LOOP_STACK_SIZE=16384`.
  - fix runtime QR/camera ownership + profil ressource auto (caméra/micro) à l’ouverture app.
- [ ] Gate endurance longue (20 cycles/app) encore à passer pour valider la sortie Sprint 2.

Effort estime:
- camera_video: 2.5 jh
- qr_scanner: 1.5 jh
- dictaphone: 2 jh
- integration/tests hardware: 1 jh
- Total: 7 jh

Risque:
- Eleve (camera + audio + FS + contention)

Gate sortie:
- Snapshot/photo/record/list/delete valides.
- Pas de crash sur enchainement camera <-> dictaphone.

### Sprint 3 (Semaine 4) - Audio Pack P1

Apps:
- `audio_player`
- `audiobook_player`
- `kids_webradio`
- `kids_podcast`
- `kids_music`

Objectif:
- Unifier la stack audio locale/streaming + fallback offline.

Effort estime:
- audio_player: 2 jh
- audiobook_player: 2 jh
- declinaisons kids_media (3 apps): 2 jh
- integration/tests reseau/offline: 1.5 jh
- Total: 7.5 jh

Risque:
- Eleve (Wi-Fi instable, URLs, fallback)

Gate sortie:
- Streaming fonctionne si Wi-Fi present.
- Fallback offline automatique si Wi-Fi absent.
- Reprise audiobook (progress + bookmark) apres reboot.

### Sprint 4 (Semaine 5) - Kids Learning/Creative Pack P2

Apps:
- `kids_drawing`
- `kids_coloring`
- `kids_yoga`
- `kids_meditation`
- `kids_languages`
- `kids_math`
- `kids_science`
- `kids_geography`

Objectif:
- Finaliser les 2 familles modules: `KidsCreativeModule` et `KidsLearningModule`.

Effort estime:
- base creative (2 apps): 3 jh
- base learning (6 apps): 4 jh
- contenus/fixtures progression: 1.5 jh
- tests endurance: 1 jh
- Total: 9.5 jh

Risque:
- Moyen (beaucoup d'apps, logique mutualisee)

Gate sortie:
- Sauvegarde/reprise validee pour creative + learning.
- Score/progression lesson persistants pour learning apps.

### Sprint 5 (Semaine 6) - NES + Hardening global P2

Apps:
- `nes_emulator`

Objectif:
- Verrouiller la qualite de fin de lot sur les 20 apps.

Effort estime:
- NES core integration UI/input: 3 jh
- hardening global (errors, watchdog, retry): 2 jh
- regression matrix 20 apps: 2 jh
- Total: 7 jh

Risque:
- Moyen a eleve (charge CPU/loop timing)

Gate sortie:
- Validation ROM mapper0 + controls de base.
- Regression matrix complete verte.

## 3. Matrice effort/risque par application

| App | Module | Priorite | Effort (jh) | Risque |
|---|---|---:|---:|---|
| audio_player | AudioPlayerModule | P1 | 2.0 | Eleve |
| audiobook_player | AudiobookModule | P1 | 2.0 | Moyen |
| calculator | CalculatorModule | P1 | 1.0 | Faible |
| timer_tools | TimerToolsModule | P1 | 1.5 | Faible |
| flashlight | FlashlightModule | P1 | 1.0 | Faible |
| camera_video | CameraVideoModule | P1 | 2.5 | Eleve |
| qr_scanner | QrScannerModule | P1 | 1.5 | Moyen |
| dictaphone | DictaphoneModule | P1 | 2.0 | Eleve |
| kids_webradio | AudioPlayerModule | P1 | 0.8 | Moyen |
| kids_podcast | AudioPlayerModule | P1 | 0.8 | Moyen |
| kids_music | AudioPlayerModule | P1 | 0.8 | Moyen |
| kids_drawing | KidsCreativeModule | P2 | 1.5 | Moyen |
| kids_coloring | KidsCreativeModule | P2 | 1.5 | Moyen |
| kids_yoga | KidsLearningModule | P2 | 1.2 | Moyen |
| kids_meditation | KidsLearningModule | P2 | 1.2 | Moyen |
| kids_languages | KidsLearningModule | P2 | 1.2 | Moyen |
| kids_math | KidsLearningModule | P2 | 1.2 | Moyen |
| kids_science | KidsLearningModule | P2 | 1.2 | Moyen |
| kids_geography | KidsLearningModule | P2 | 1.2 | Moyen |
| nes_emulator | NesEmulatorModule | P2 | 3.0 | Eleve |

## 4. Risques transverses et mitigation

1. Couplage launcher/runtime incomplet
- Mitigation: fermer Sprint 0 avant toute extension app.

2. Contention camera/audio/LVGL
- Mitigation: tests de bascule entre apps capture/audio a chaque sprint.

3. Variabilite Wi-Fi sur apps streaming
- Mitigation: fallback offline obligatoire et tests en mode deconnecte.

4. Regression silencieuse des actions app
- Mitigation: suite smoke serial/API standardisee (`APP_OPEN`, `APP_ACTION`, `APP_CLOSE`).

## 5. Definition of Done globale

1. 20/20 apps ouvrables depuis launcher + API.
2. Chaque app execute au moins 3 actions metier sans erreur critique.
3. Retour launcher stable apres 50 cycles open/close mixes.
4. Build firmware + buildfs + upload smoke validates.

## 6. Execution immediate proposee

Ordre concret des 10 prochains jours:
1. J1-J2: Sprint 0 (launcher runtime bridge + retour launcher)
2. J3-J4: Sprint 1 (calculator/timer/flashlight)
3. J5-J7: Sprint 2 (camera/qr/dictaphone)
4. J8-J10: Debut Sprint 3 (audio_player + audiobook_player)
