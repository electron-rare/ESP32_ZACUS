# Spec App - Chronometre/Minuteur (`timer_tools`)

## 1. Cadrage
- ID runtime: `timer_tools`
- Catégorie: `utility`
- Scène d'entrée: `SCENE_TIMER`
- Module actuel: `TimerToolsModule`
- Manifest attendu: `/apps/timer_tools/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (128): `CAP_GPU_UI`
- Optionnelles (0): `AUCUNE`

## 3. Objectif Produit
- Stopwatch + countdown non bloquants dans la boucle runtime.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `sw_start` | `vide` | Démarre le chrono. |
| `sw_stop` | `vide` | Stoppe le chrono. |
| `sw_lap` | `vide` | Capture un lap courant. |
| `sw_reset` | `vide` | Reset chrono + lap. |
| `cd_set` | `durée sec` ou `{"seconds":N}` | Configure le compte à rebours. |
| `cd_start` | `vide` | Démarre le compte à rebours. |
| `cd_pause` | `vide` | Pause en conservant le restant. |
| `cd_reset` | `vide` | Reset compte à rebours. |
| `status` | `vide` | Publie `sw=<ms> cd=<ms> run=<0|1>` dans `last_event`. |

## 5. Stockage & Persistance
- Aucune persistance actuellement (état perdu au stop/reboot).

## 6. Backlog Développement (MVP)
- Affichage temps réel propre du chrono et countdown.
- Signal fin de countdown (audio + visuel).
- Option persistance état session (v2).
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN timer_tools` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `TimerToolsModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
