# Spec App - Appareil Photo/Video (`camera_video`)

## 1. Cadrage
- ID runtime: `camera_video`
- Catégorie: `capture`
- Scène d'entrée: `SCENE_PHOTO_MANAGER`
- Module actuel: `CameraVideoModule`
- Manifest attendu: `/apps/camera_video/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (196): `CAP_CAMERA`, `CAP_STORAGE_FS`, `CAP_GPU_UI`
- Optionnelles (32): `CAP_STORAGE_SD`

## 3. Objectif Produit
- Prévisualisation caméra, snapshots et pseudo-clip (index de frames).

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `preview_on` | `vide` | Démarre la caméra/preview. |
| `preview_off` | `vide` | Stoppe la preview caméra. |
| `snapshot` | `vide` | Capture une image via snapshotToFile. |
| `clip_start` | `vide` | Démarre la collecte de frames de clip. |
| `clip_stop` | `vide` | Stoppe le clip et écrit un index JSON. |
| `list_media` | `vide` | Liste les médias caméra disponibles. |
| `delete_media` | `path` | Supprime un média caméra. |

## 5. Stockage & Persistance
- Index clip: /apps/camera_video/cache/clip_<timestamp>.index.json
- Index contient started_at_ms, stopped_at_ms, frames[]

## 6. Backlog Développement (MVP)
- Capture photo fiable (latence + nommage).
- Gestion erreurs caméra (start/snapshot/list/delete).
- Écran galerie minimal pour consultation/suppression.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN camera_video` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `CameraVideoModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
