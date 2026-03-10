# Spec App - Emulateur NES (`nes_emulator`)

## 1. Cadrage
- ID runtime: `nes_emulator`
- Catégorie: `games`
- Scène d'entrée: `SCENE_NES_EMU`
- Module actuel: `NesEmulatorModule`
- Manifest attendu: `/apps/nes_emulator/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (193): `CAP_AUDIO_OUT`, `CAP_STORAGE_FS`, `CAP_GPU_UI`
- Optionnelles (32): `CAP_STORAGE_SD`

## 3. Objectif Produit
- MVP émulation NES déterministe (mapper 0) avec pilotage runtime.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `list_roms` | `vide` | Compte les ROMs .nes disponibles. |
| `rom_validate` | `path .nes` | Valide header iNES + contraintes mapper. |
| `rom_start` | `path + {fps}` | Charge la ROM et démarre exécution. |
| `rom_stop` | `vide` | Stoppe l'émulation. |
| `set_fps` | `30..60` | Ajuste la cadence cible. |
| `input` | `mask entier` | Envoie un masque d'input global. |
| `btn_down` | `bit index` | Active un bouton. |
| `btn_up` | `bit index` | Relâche un bouton. |
| `core_status` | `vide` | Expose état rom/fps/frame/input/hash. |

## 5. Stockage & Persistance
- ROMs: /apps/nes_emulator/roms/*.nes
- Taille max ROM: 1 MiB
- Contraintes: signature NES, mapper=0, trainer non supporté

## 6. Backlog Développement (MVP)
- Sélecteur ROM + démarrage/arrêt propre.
- Mapping input manette depuis boutons/tactile.
- Overlay stats FPS + erreurs ROM.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN nes_emulator` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `NesEmulatorModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
