# Spec App - Yoga Enfants (`kids_yoga`)

## 1. Cadrage
- ID runtime: `kids_yoga`
- Catégorie: `kids_wellness`
- Scène d'entrée: `SCENE_KIDS_YOGA`
- Module actuel: `KidsLearningModule`
- Manifest attendu: `/apps/kids_yoga/manifest.json`
- Offline: **oui** | Streaming: **oui**

## 2. Capacités
- Requises (129): `CAP_AUDIO_OUT`, `CAP_GPU_UI`
- Optionnelles (80): `CAP_WIFI`, `CAP_STORAGE_FS`

## 3. Objectif Produit
- Parcours pédagogique guidé (leçons, quiz, session audio).

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `lesson_open` | `lesson id` | Ouvre une leçon et reset le step. |
| `lesson_next` | `vide` | Passe à l'étape suivante. |
| `lesson_prev` | `vide` | Revient à l'étape précédente. |
| `quiz_answer` | `answer` | Met à jour le score si réponse correcte. |
| `session_start / play` | `path/url optionnel` | Lance audio guidé. |
| `pause` | `vide` | Pause audio + sauvegarde progression. |
| `session_stop / stop` | `vide` | Stop audio + sauvegarde. |
| `seek_ms` | `entier ms` | Met à jour curseur logique. |

## 5. Stockage & Persistance
- Progression: /apps/<app_id>/progress.json
- Cursor: lesson, step, score, cursor_ms
- Audio offline recommandé: /apps/<app_id>/audio/session.mp3

## 6. Backlog Développement (MVP)
- Flow leçon/quiz complet et robuste.
- Persistance fine de progression par app.
- Fallback audio offline si Wi-Fi indisponible.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN kids_yoga` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `KidsLearningModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
