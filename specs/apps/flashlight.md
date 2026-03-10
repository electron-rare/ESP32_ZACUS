# Spec App - Lampe de Poche (`flashlight`)

## 1. Cadrage
- ID runtime: `flashlight`
- Catégorie: `utility`
- Scène d'entrée: `SCENE_FLASHLIGHT`
- Module actuel: `FlashlightModule`
- Manifest attendu: `/apps/flashlight/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (136): `CAP_LED`, `CAP_GPU_UI`
- Optionnelles (0): `AUCUNE`

## 3. Objectif Produit
- Contrôle LED manuel (on/off/intensité).

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `light_on` | `vide` | Allume la LED en blanc avec intensité courante. |
| `light_off` | `vide` | Éteint la LED. |
| `light_toggle` | `vide` | Alterne on/off en conservant l'intensité. |
| `set_level` | `0..255` ou `{"level":N}` | Met à jour l'intensité (appliquée à chaud si allumé). |
| `status` | `vide` | Publie `on=<0|1> level=<N>` dans `last_event`. |

## 5. Stockage & Persistance
- Pas de persistance actuelle de l'intensité.

## 6. Backlog Développement (MVP)
- Slider intensité + bouton toggle fiable.
- Sécurité thermique/autonomie (limite max configurable).
- Restauration niveau précédent (v2).
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN flashlight` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `FlashlightModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
