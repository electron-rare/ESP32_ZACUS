# Spec UI - Grille d'Apps type Workbench Amiga

## 1. Cadrage
- Scope: launcher principal Zacus (avant ouverture app).
- Cible: Freenove ESP32-S3, UI LVGL + `AmigaUIShell`.
- Source apps: `data/apps/registry.json` (20 apps actives).
- Contrainte: flux stable `Launcher -> App -> Retour Launcher` sans freeze.

## 2. Objectif Produit
- Offrir une grille d'icones claire, rapide, et "Workbench-like":
  - lecture immediate des apps disponibles,
  - navigation tactile et boutons physiques,
  - ouverture fiable via runtime apps,
  - feedback explicite sur l'etat (`starting`, `running`, `failed`).

## 3. Identite visuelle (Workbench)
- Direction:
  - fond bleu/gris desature + bandeau haut systeme,
  - icones carrees pixel-art (retro propre, pas flou),
  - labels courts en dessous de chaque icone,
  - focus rectangle net et contraste fort.
- Hierarchie:
  - barre top: heure, wifi, batterie, mode runtime,
  - zone centrale: grille paginee,
- Typo:
  - titre/labels: police pixel existante du projet,
  - fallback lisible sur petits ecrans.

## 4. Modele de donnees UI
- Entree brute (registry):
  - `id`, `title`, `category`, `icon_path`, `enabled`, `entry_screen`, `required_capabilities`.
- Modele local "tile":
  - `id` (string),
  - `title` (string),
  - `category` (enum),
  - `icon_path` (string),
  - `enabled` (bool),
  - `state` (enum: `idle|starting|running|error`),
  - `badge` (optionnel: `new|wifi|required`),
  - `last_error` (optionnel).
- Tri par defaut:
  - 1. apps `enabled=true`,
  - 2. categorie (utility, capture, media, kids*, emulator),
  - 3. ordre registry stable.

## 5. Layout grille
- Grille portrait:
  - écran 160*480
  - 3 colonnes x 4 lignes (12 tuiles/page) si densite standard.
- Grille paysage:
  - 4 colonnes x 3 lignes (12 tuiles/page).
- Dimensions tuile:
  - zone touch >= 64x64,
  - icone visible 40-56 px selon densite,
  - label sur 1 ligne (ellipsis au besoin).
- Pagination:
  - swipe horizontal tactile,
  - boutons gauche/droite,
  - indicateur de page discret.

## 6. Interactions
- Touch:
  - tap court sur tuile: `APP_OPEN <id> default`.
  - long press: menu contextuel (`ouvrir`, `details`, `tester action`).
- Boutons:
  - `UP/DOWN/LEFT/RIGHT`: deplacement focus,
  - `A` (ou `OK`): ouvrir app focus,
  - `B` (ou `BACK`): retour page precedente / fermer panneau info,
  - `MENU`: ouvrir options launcher.
- Etat launching:
  - lock anti double-launch pendant `starting`,
  - spinner + texte `Ouverture <app>...`.

## 7. Contrat runtime (integration)
- Ouvrir:
  - via bridge `AmigaUIShell::requestOpenApp(app_id, mode, source)`.
- Fermer:
  - via `AmigaUIShell::requestCloseApp(reason)` ou retour scene idle.
- Status:
  - polling `currentStatus()` / `APP_STATUS` pour sync etat tuile active.
- Erreurs:
  - mapper `resource_busy`, `missing_asset`, `camera_start_failed`, `record_start_failed` vers messages UI courts.

## 8. Gestion icones/assets
- Regle chargement:
  - charger uniquement page courante + page voisine (prefetch).
- Fallback:
  - si icone absente/corrompue => icone categorie par defaut.
- Cache:
  - cache LRU simple en RAM interne/PSRAM selon dispo,
  - invalidation sur changement de page ou refresh registry.

## 9. Performance / memoire
- KPI cible launcher:
  - navigation sans lag perceptible (input < 100 ms),
  - pas de freeze en switch page/focus,
  - aucune panic DMA/LVGL.
- Guardrails:
  - pas d'allocation dynamique lourde a chaque frame,
  - debounce sur ouverture app,
  - rendu incremental (pas de redraw full frame inutile).

## 10. Etats & erreurs UX
- `idle`: grille normale.
- `starting`: tuile active marquee + overlay progression.
- `running`: launcher cache, app affichee.
- `error`: toast + retour focus sur tuile source.
- Messages utilisateur courts:
  - `Ressource occupee`,
  - `Fichier manquant`,
  - `Camera indisponible`,
  - `Erreur ouverture`.

## 11. Telemetrie & logs
- Logs serie attendus:
  - `APP_OPEN_OK|APP_OPEN_FAIL`,
  - `APP_CLOSE_OK|APP_CLOSE_FAIL`,
  - `APP_ACTION_OK|APP_ACTION_FAIL`.
- Logs UI launcher:
  - `UI_AMIGA launch id=<app> page=<n> focus=<i>`,
  - `UI_AMIGA open_fail id=<app> err=<code>`.

## 12. Critères d'acceptation
1. Les 20 apps visibles via pagination sans crash.
2. Ouverture/fermeture app depuis grille fonctionne sur tactile + boutons.
3. Anti double-launch fonctionne (aucune double ouverture concurente).
4. En cas d'echec runtime, feedback UI explicite et focus restaure.
5. Retour launcher toujours possible apres `APP_CLOSE`.

## 13. Scenarios de test
1. Parcours complet:
   - parcourir toutes les pages, ouvrir 1 app par categorie, fermer et revenir.
2. Stress navigation:
   - 100 changements focus + 30 ouvertures/fermetures mixees.
3. Erreurs simulees:
   - app desactivee, manifest manquant, ressource indisponible.
4. Coherence etat:
   - comparer etat tuile active vs `APP_STATUS`.

## 14. Backlog implementation
- P0:
  - composant tuile + focus + pagination + open/close bridge.
- P1:
  - cache icones + badges etats + panneau details app.
- P2:
  - edition ordre/favoris + recherche alphabetique locale.

## 15. Audit implementation actuel (2026-03-02)

Etat observe dans `ui_amiga_shell`:
- [x] Bridge runtime present (`requestOpenApp`, `requestCloseApp`, `currentStatus`).
- [x] Anti double-launch present (debounce + garde etat runtime).
- [~] Rendu grille present, mais non pagine.
- [ ] Navigation tactile complete 20 apps (actuellement limitee au grid 4x4).
- [ ] Emulation tactile complete 20 apps (actuellement initialisee en 4x4).
- [ ] Interactions Workbench cibles (`LEFT/RIGHT`, long-press, panel details) non finalisees.

Ecarts techniques a fermer en priorite:
1. Grille hardcodee `GRID_COLS=4`, `GRID_ROWS=4` avec 20 apps -> il faut pagination ou layout dynamique.
2. `getTouchGridIndex` borne Y sur 4 rangees -> apps index 16..19 non touchables.
3. `setTouchEmulationMode` initialise l'emulateur en 4x4 -> impossible d'atteindre toutes les apps via emulation.
4. `playTransitionFX` et `handleTouchInput` utilisent `delay()` bloquants -> a remplacer par sequence non bloquante.

Definition de done Workbench (deblocage sprint):
- 20 apps visibles et accessibles (touch + boutons + emulation).
- Aucun `delay()` bloquant dans le flux launch.
- Pagination/focus/page-state synchronises avec `APP_STATUS`.
