# ESP32_ZACUS

Workspace firmware centré sur une seule cible: Freenove FNK0102H avec `ui_freenove_allinone`.

## Docs canoniques

- `docs/QUICKSTART.md`: bootstrap, build, flash, monitor, smoke.
- `docs/FNK0102H_SOURCE_OF_TRUTH.md`: matrice board/config/pins/support.
- `README_ESP32_ZACUS.md`: notes runtime et signatures série utiles.

## Arborescence utile

- `platformio.ini`: env PlatformIO canonique `freenove_esp32s3_full_with_ui`.
- `ui_freenove_allinone/`: firmware UI/audio/caméra/réseau.
- `data/`: contenu LittleFS.
- `lib/`: bibliothèques runtime.
- `scripts/`: bootstrap et scripts repo.
- `tests/`: harness Sprint 1, Sprint 2, Sprint 3, Phase 9.

## Environnements supportés

- `freenove_esp32s3_full_with_ui`: chemin release/stabilisation.
- `freenove_esp32s3`: alias du chemin canonique.
- `freenove_esp32s3_touch`: expérimental, hors release, sans CI.

## Démarrage rapide

```bash
./scripts/bootstrap_platformio.sh
./scripts/doctor_repo.sh
source .venv/bin/activate
python -m platformio run -e freenove_esp32s3_full_with_ui
python -m platformio run -e freenove_esp32s3_full_with_ui -t buildfs
python -m platformio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port <PORT>
python -m platformio run -e freenove_esp32s3_full_with_ui -t upload --upload-port <PORT>
python -m platformio device monitor -b 115200 --port <PORT>
```

## Guardrails runtime

- Si la PSRAM est détectée, le firmware démarre normalement puis autorise le réseau après validation UI.
- Si la PSRAM n'est pas détectée, le firmware bascule en `safe diagnostic mode`.
- En mode diagnostic, le firmware garde uniquement la pile série, l'affichage minimal et les boutons physiques.
- En mode diagnostic, caméra, FX, micro, audio, Wi-Fi, ESP-NOW, WebUI et partage de fichiers restent coupés.

## Vérifications minimales

```bash
./scripts/doctor_repo.sh
python tests/sprint1_utility_contract.py --mode serial --port <PORT>
python tests/sprint2_capture_contract.py --mode serial --port <PORT>
python tests/sprint3_audio_contract.py --mode serial --port <PORT>
python tests/phase9_ui_validation.py --port <PORT>
```























<!-- CHANTIER:AUDIT START -->
## Audit & Execution Plan (2026-03-10)

### Snapshot
- Priority: `P2`
- Tech profile: `embedded`
- Workflows: `yes`
- Tests: `yes`
- Debt markers: `0`
- Source files: `258`

### Corrections Prioritaires
- [ ] Vérifier target PlatformIO et budget mémoire
- [ ] Ajouter/fiabiliser les commandes de vérification automatiques.
- [ ] Clore les points bloquants avant optimisation avancée.

### Optimisation
- [ ] Identifier le hotspot principal et mesurer avant/après.
- [ ] Réduire la complexité des modules les plus touchés.

### Mémoire chantier
- Control plane: `/Users/electron/.codex/memories/electron_rare_chantier`
- Repo card: `/Users/electron/.codex/memories/electron_rare_chantier/REPOS/ESP32_ZACUS.md`

<!-- CHANTIER:AUDIT END -->
