# Spec App - Livres Audio (`audiobook_player`)

## 1. Cadrage
- ID runtime: `audiobook_player`
- Catégorie: `media`
- Scène d'entrée: `SCENE_AUDIOBOOK`
- Module actuel: `AudiobookModule`
- Manifest attendu: `/apps/audiobook_player/manifest.json`
- Offline: **oui** | Streaming: **oui**

## 2. Capacités
- Requises (193): `CAP_AUDIO_OUT`, `CAP_STORAGE_FS`, `CAP_GPU_UI`
- Optionnelles (48): `CAP_WIFI`, `CAP_STORAGE_SD`

## 3. Objectif Produit
- Lecture de livres audio avec position et signets persistants.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `open_book` | `path local ou URL` | Sélectionne le livre courant. |
| `play` | `optionnel path/URL` | Lance lecture locale ou streaming. |
| `pause` | `vide` | Met en pause et sauvegarde la progression. |
| `stop` | `vide` | Arrête et sauvegarde la progression. |
| `seek_ms` | `entier ms` | Met à jour la position de lecture logique. |
| `bookmark_set` | `entier ms` | Positionne le signet courant. |
| `bookmark_go` | `vide` | Recharge la position du signet. |

## 5. Stockage & Persistance
- Progression: /apps/audiobook_player/progress.json
- Champs persistés: book, position_ms, bookmark_ms, updated_at_ms

## 6. Backlog Développement (MVP)
- Écran bibliothèque + reprise “continuer”.
- Signets persistants et fiables après reboot.
- Parité comportement local vs URL.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN audiobook_player` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `AudiobookModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
