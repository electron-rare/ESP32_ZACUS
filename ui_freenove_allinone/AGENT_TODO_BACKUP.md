## Validation hardware – TODO détaillée

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
  - Verdict (2026-03-02): échec à `calculator cycle 9/20` avec reboot panic (`reset_reason=4`) et `APP_STATUS parse failed` post-reboot.
  - Contexte: issue non fonctionnelle app métier, liée à stabilité runtime/mémoire sous endurance.
- [ ] Gate HTTP toujours bloqué côté reachability/auth (`/api/apps/*` non validé dans cette passe).

- [x] Compiler et flasher le firmware sur le Freenove Media Kit (`pio run -e freenove_esp32s3_full_with_ui -t upload`)
- [ ] Préparer les fichiers de test sur LittleFS (/data/scene_*.json, /data/screen_*.json, /data/*.wav)
- [ ] Vérifier l’affichage TFT (boot, écran dynamique, transitions)
- [ ] Tester la réactivité tactile (zones, coordonnées, mapping)
- [ ] Tester les boutons physiques (appui, mapping, transitions)
- [ ] Tester la lecture audio (fichiers présents/absents, fallback)
- [~] Observer les logs série (initialisation, actions, erreurs) en continu pendant endurance longue
- [ ] Générer et archiver les logs d’évidence (logs/)
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

# TODO Agent – Firmware All-in-One Freenove


## Plan d’intégration détaillé (COMPLÉTÉ)

- [x] Vérifier la présence du scénario par défaut sur LittleFS
- [x] Charger la liste des fichiers de scènes et d’écrans (data/)
- [x] Initialiser la navigation UI (LVGL, écrans dynamiques)
- [x] Mapper les callbacks boutons/tactile vers la navigation UI
- [x] Préparer le fallback LittleFS si fichier manquant
- [x] Logger l’initialisation (logs/)

- [x] Boucle principale d’intégration
	- [x] Navigation UI (LVGL, écrans dynamiques)
	- [x] Exécution scénario (lecture, actions, transitions)
	- [x] Gestion audio (lecture, stop, files LittleFS)
	- [x] Gestion boutons/tactile (événements, mapping)
	- [x] Gestion stockage (LittleFS, fallback)
	- [x] Logs/artefacts

## Validation hardware (EN COURS)

- [ ] Tests sur Freenove Media Kit (affichage, audio, boutons, tactile)
- [ ] Génération de logs d’évidence (logs/)
- [ ] Production d’artefacts de validation (artifacts/)

## Documentation

- [ ] Mise à jour README.md (usage, build, structure)
- [ ] Mise à jour AGENT_FUSION.md (règles d’intégration, conventions)
- [ ] Synchronisation avec la doc onboarding principale

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

### Plan d’intégration recommandé (ordre de dépendance)

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
- [ ] Créer écran home "kids shell" (grille d’apps colorée + navigation simple)
- [ ] Ajouter transitions ludiques non bloquantes (sans pénaliser FPS/runtime loop)
- [ ] Standardiser composants UI: bouton, carte app, badge état, feedback action
- [ ] Ajouter règles de lisibilité enfant (texte court, contraste, zones tactiles larges)

## Progression technique réalisée (itération courante)

- [x] Socle apps runtime: registre, lifecycle manager, endpoints `/api/apps/*`
- [x] Contrat capacités runtime: flags explicites et gating au lancement d’app
- [x] Actions scénario app-aware: `open_app`, `close_app`, `app_action`
- [x] Bonjour/mDNS: service publié `_zacus._tcp` + découverte peers
- [x] Partage fichiers simple: endpoints `/api/share/peers`, `/api/share/files`, `/api/share/upload`
- [x] Spécifications JSON: manifest/streams/progress + registre exemple
- [x] Direction UX enfant + Amiga: fichier de thème de référence
