# Spec App - Dessin (`kids_drawing`)

## 1. Cadrage
- ID runtime: `kids_drawing`
- Catégorie: `kids_learning`
- Scène d'entrée: `SCENE_DRAWING`
- Module actuel: `KidsCreativeModule`
- Manifest attendu: `/apps/kids_drawing/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (128): `CAP_GPU_UI`
- Optionnelles (0): `AUCUNE`

## 3. Objectif Produit
- Apps créatives dessin/coloriage avec sauvegarde canvas.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `open_canvas` | `{"color":"#RRGGBB"}` | Ouvre une session canvas avec couleur active. |
| `stroke` | `JSON ou couleur` | Ajoute un trait logique. |
| `fill` | `JSON ou couleur` | Applique un remplissage logique. |
| `undo` | `vide` | Annule le dernier trait logique. |
| `clear / clear_canvas` | `vide` | Réinitialise canvas. |
| `save` | `path optionnel` | Sauvegarde le canvas. |
| `load` | `path optionnel` | Recharge le canvas. |

## 5. Stockage & Persistance
- Canvas défaut: /apps/<app_id>/content/last_canvas.json
- Format: id, updated_at_ms, stroke_count, color

## 6. Backlog Développement (MVP)
- Moteur de dessin tactile (coordonnées + couleurs + undo).
- Sauvegarde/reprise stable du canvas.
- Palette adaptée enfant + UX simple.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN kids_drawing` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `KidsCreativeModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
