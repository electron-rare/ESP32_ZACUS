# Spec App - Musique Enfants (`kids_music`)

## 1. Cadrage
- ID runtime: `kids_music`
- Catégorie: `kids_media`
- Scène d'entrée: `SCENE_KIDS_MUSIC`
- Module actuel: `AudioPlayerModule`
- Manifest attendu: `/apps/kids_music/manifest.json`
- Offline: **oui** | Streaming: **oui**

## 2. Capacités
- Requises (129): `CAP_AUDIO_OUT`, `CAP_GPU_UI`
- Optionnelles (80): `CAP_WIFI`, `CAP_STORAGE_FS`

## 3. Objectif Produit
- Lecture audio locale/streaming avec fallback offline automatique.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `play` | `path texte (ex: /music/boot_radio.mp3)` | Lance la lecture locale. |
| `play_url` | `URL http(s)` | Lance le streaming; fallback offline si Wi-Fi indisponible. |
| `pause` | `vide` | Stoppe la lecture en gardant la piste courante pour reprise. |
| `resume` | `vide` | Reprend la lecture locale ou URL mémorisée. |
| `stop` | `vide` | Arrête la lecture. |
| `set_volume` | `0..100` | Ajuste le volume. |
| `next` | `vide` | Non implémenté (retour playlist_not_configured). |
| `prev` | `vide` | Non implémenté (retour playlist_not_configured). |

## 5. Stockage & Persistance
- Fallback offline: /apps/<app_id>/audio/offline.mp3
- Fallback default: /apps/<app_id>/audio/default.mp3
- Fallback global: /music/boot_radio.mp3

## 6. Backlog Développement (MVP)
- UI lecture/pause/stop/volume stable.
- Indicateur explicite quand fallback offline est utilisé.
- Gestion d'erreur visible pour missing_asset/network_unavailable.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN kids_music` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `AudioPlayerModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
