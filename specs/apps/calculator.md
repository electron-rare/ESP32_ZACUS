# Spec App - Calculatrice (`calculator`)

## 1. Cadrage
- ID runtime: `calculator`
- Catégorie: `utility`
- Scène d'entrée: `SCENE_CALCULATOR`
- Module actuel: `CalculatorModule`
- Manifest attendu: `/apps/calculator/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (128): `CAP_GPU_UI`
- Optionnelles (0): `AUCUNE`

## 3. Objectif Produit
- Évaluation d'expressions mathématiques basiques.

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `eval` | `expression texte` | Calcule le résultat (tinyexpr ou parser interne). |
| `clear` | `vide` | Réinitialise le résultat. |
| `status` | `vide` | Retourne le résultat courant via `last_event` (`result=...`). |

## 5. Stockage & Persistance
- Pas de persistance (historique non stocké).

## 6. Backlog Développement (MVP)
- Clavier calculatrice + affichage résultat.
- Gestion explicite des erreurs d'expression (eval_error).
- Historique local (v2).
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN calculator` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `CalculatorModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
