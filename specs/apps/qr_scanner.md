# Spec App - Lecteur QR Code (`qr_scanner`)

## 1. Cadrage
- ID runtime: `qr_scanner`
- Catégorie: `capture`
- Scène d'entrée: `SCENE_QR_DETECTOR`
- Module actuel: `QrScannerModule`
- Manifest attendu: `/apps/qr_scanner/manifest.json`
- Offline: **oui** | Streaming: **non**

## 2. Capacités
- Requises (132): `CAP_CAMERA`, `CAP_GPU_UI`
- Optionnelles (0): `AUCUNE`

## 3. Objectif Produit
- Scan QR avec classification basique des payloads (url/app/text).

## 4. Contrat Actions (`APP_ACTION` / `/api/apps/action`)
| Action | Payload | Effet attendu |
|---|---|---|
| `scan_start` | `vide` | Active le mode scan continu logique. |
| `scan_stop` | `vide` | Désactive le mode scan continu logique. |
| `scan_once` | `payload détecté` | Classe en url/app/text/unknown. |

## 5. Stockage & Persistance
- Pas de persistance dédiée dans le module actuel.

## 6. Backlog Développement (MVP)
- Bridge vers déclenchement action selon type (URL/app/texte).
- UI feedback immédiat: type + contenu + action possible.
- Timeout/retry robustes quand caméra indisponible.
- Intégrer le déclenchement depuis le launcher (`AmigaUIShell::launchSelectedApp`) via `APP_OPEN`.
- Vérifier la route de scène (`entry_screen`) et la cohérence UI/LVGL.

## 7. Critères d’Acceptation
- L'app se lance via API et via commande série sans crash.
- Les actions principales répondent sans erreur non gérée.
- Le statut runtime reflète correctement `state`, `last_event`, `last_error`.
- Retour propre à `SCENE_READY` après fermeture app.

## 8. Tests Recommandés
1. `APP_OPEN qr_scanner` puis contrôle `UI_SCENE_STATUS`.
2. Exécuter 3-5 actions clés de `QrScannerModule` (serial/API).
3. `APP_CLOSE` puis validation `APP_IDLE` et absence de fuite mémoire visible.
