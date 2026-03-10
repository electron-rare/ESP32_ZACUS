# Spec App - Dictaphone (`dictaphone`)

## 1. Cadrage
- ID runtime: `dictaphone`
- Catégorie: `capture`
- Scène d'entrée: `SCENE_RECORDER`
- Module actuel: `DictaphoneModule`
- Manifest attendu: `/apps/dictaphone/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (194): `CAP_AUDIO_IN`, `CAP_STORAGE_FS`, `CAP_GPU_UI`
- Optionnelles (32): `CAP_STORAGE_SD`

## 3. Objectif Produit
- Enregistrement audio simple, lecture et gestion des fichiers.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `record_start` | `durée sec (défaut 30)` | Démarre l'enregistrement. |
| `record_stop` | `vide` | Stoppe l'enregistrement. |
| `play_file` | `path` | Lit un enregistrement. |
| `delete_file` | `path` | Supprime un enregistrement. |
| `list_records` | `vide` | Liste les enregistrements. |

## 5. Stockage & Persistance
- Gestion via MediaManager (namespace records).
- Prévoir convention de nommage unique horodatée.

## 6. Backlog Développement (MVP)
- Workflow record -> stop -> replay validé en boucle.
- UI liste + suppression sécurisée.
- Messages d'erreur clairs (record_start_failed, play_file_failed, etc.).
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN dictaphone` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `DictaphoneModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
