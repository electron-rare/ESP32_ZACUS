# Optimisation automatique hardware
Depuis février 2026, le cockpit détecte automatiquement le hardware connecté (port série, ID USB) et ne build/flash que l’environnement PlatformIO correspondant.

Pour builder ou flasher :
- Menu cockpit : "Build all firmware" ou "Flash auto (hardware détecté)"
- Ligne de commande : ./tools/dev/cockpit.sh flash (auto) ou ./tools/dev/cockpit.sh build (auto)

La détection est basée sur pyserial et l’ID USB (Freenove, ESP32, ESP8266, RP2040). Le log affiche le hardware détecté et l’environnement PlatformIO ciblé.

Pour forcer un build/flash spécifique : ./tools/dev/cockpit.sh flash <env>

## Statut Freenove ESP32-S3 (2026-03-01)

- Build firmware: OK (`freenove_esp32s3_full_with_ui`)
- Build/flash LittleFS: OK
- Flash firmware: OK
- Smoke série runtime: KO (reboot loop `rst:0x3`, `Saved PC:0x403cdb0a`)
- Détail et plan de debug: `README_ESP32_ZACUS.md`

# Hardware Firmware

> **[Mise à jour 2026]**
>
> **Tous les assets LittleFS (scénarios, écrans, scènes, audio, actions, etc.) sont désormais centralisés dans le dossier `data/` à la racine de `hardware/firmware/`.**
>
> Ce dossier unique sert de source pour le flash LittleFS sur ESP32, ESP8266 et RP2040. Les anciens dossiers `data/` dans les sous-projets doivent être migrés/supprimés (voir encart migration ci-dessous).


Workspace PlatformIO unique pour 3 firmwares:

- `esp32_audio/`: firmware principal audio
- `ui/esp8266_oled/`: UI OLED legere
- `ui/rp2040_tft/`: UI TFT tactile (LVGL)
- `protocol/`: contrat UART partage (`ui_link_v2.md`, `ui_link_v2.h`)
### 🟦 Freenove Media Kit (RP2040)

Un environnement PlatformIO dédié est disponible pour le boîtier Freenove Media Kit :

- **Build** : `pio run -e ui_rp2040_freenove`
- **Flash** : `pio run -e ui_rp2040_freenove -t upload --upload-port <PORT>`
- **Monitor** : `pio device monitor -e ui_rp2040_freenove --port <PORT>`

Le mapping hardware (pins, écran, boutons) est défini dans `ui/rp2040_tft/include/ui_freenove_config.h`.

**Schéma de branchement** : voir `hardware/wiring/wiring.md` (section Freenove)

**Remarque** : adaptez les defines dans `ui_freenove_config.h` selon votre version du Media Kit (écran, boutons, etc.).


## 📚 Documentation

- **[État des lieux](docs/STATE_ANALYSIS.md)** - Analyse complète du firmware
- **[Recommandations Sprint](docs/SPRINT_RECOMMENDATIONS.md)** - Actions prioritaires
- **[Recovery WiFi/AP & Health](docs/WIFI_RECOVERY_AND_HEALTH.md)** - Procédure recovery AP, healthcheck, troubleshooting

Pour débuter : [docs/QUICKSTART.md](docs/QUICKSTART.md)


## Structure des assets LittleFS (cross-plateforme)

```
hardware/firmware/data/
	story/
		scenarios/
			DEFAULT.json
		apps/
		screens/
		audio/
		actions/
	audio/
	radio/
	net/
```

**Scripts de génération et de flash : toujours pointer vers ce dossier.**

---

## Build

```sh
pio run
```

Build cible:

```sh
pio run -e esp32dev
pio run -e esp32_release
pio run -e esp8266_oled
pio run -e ui_rp2040_ili9488
pio run -e ui_rp2040_ili9486
```

Script global:

```sh
./build_all.sh
```

Bootstrap local tools once:

```sh
./tools/dev/bootstrap_local.sh
```

## Story portable (generation + runtime)

- Generation library: `lib/zacus_story_gen_ai/` (Yamale + Jinja2).
- Runtime library: `lib/zacus_story_portable/` (tinyfsm-style internals).
- Story serial protocol: JSON-lines V3 (`story.*`), see `docs/protocols/story_v3_serial.md`.
- Canonical migration doc: `docs/STORY_PORTABILITY_MIGRATION.md`.

CLI:

```sh
./tools/dev/story-gen validate
./tools/dev/story-gen generate-cpp
./tools/dev/story-gen generate-bundle
./tools/dev/story-gen all
```


## Flash (cockpit)

```sh
./tools/dev/cockpit.sh flash
```


Options utiles:

- `ZACUS_FLASH_ESP32_ENVS="esp32dev esp32_release"`
- `ZACUS_FLASH_RP2040_ENVS="ui_rp2040_ili9488 ui_rp2040_ili9486"`
- `ZACUS_PORT_ESP32=... ZACUS_PORT_ESP8266=... ZACUS_PORT_RP2040=...`


---

## Migration LittleFS (2026)

- Déplacer tous les fichiers de scénario, écrans, scènes, audio, etc. dans `hardware/firmware/data/`.
- Adapter les scripts de génération et de flash pour pointer vers ce dossier.
- Supprimer les anciens dossiers `data/` dispersés dans les sous-projets (`ui/rp2040_tft/data/`, `esp32_audio/data/`, etc.).
- Mettre à jour tous les guides et onboarding pour refléter cette structure unique.

---

```sh
make fast-esp32 ESP32_PORT=<PORT_ESP32>
make fast-ui-oled UI_OLED_PORT=<PORT_ESP8266>
make fast-ui-tft UI_TFT_PORT=<PORT_RP2040>
```

Smoke série (manuel):

```sh
python3 tools/dev/serial_smoke.py --role auto --baud 115200 --wait-port 3 --allow-no-hardware
```

MacOS CP2102 duplicates share VID/PID=10C4:EA60/0001; the LOCATION (20-6.1.1=ESP32, 20-6.1.2=ESP8266) drives the detector. `tools/dev/ports_map.json` now uses `location -> role` and `vidpid -> role` mappings.
USB console monitoring uses `115200`. ESP8266 internal UI link SoftwareSerial stays at `57600` (internal link only).

## Serial smoke commands

- baseline smoke (auto handles already connected boards): `python3 tools/dev/serial_smoke.py --role auto --baud 115200 --wait-port 3 --allow-no-hardware`
- run every detected role: `python3 tools/dev/serial_smoke.py --role all --baud 115200 --wait-port 3 --allow-no-hardware`
- force hardware detection: `ZACUS_REQUIRE_HW=1 python3 tools/dev/serial_smoke.py --role auto --baud 115200 --wait-port 180`
- skip PlatformIO builds and just run smoke (useful when downloads are impossible): `ZACUS_SKIP_PIO=1 ./tools/dev/run_matrix_and_smoke.sh`

## Build + smoke combo

```sh
./tools/dev/run_matrix_and_smoke.sh
# or from repo root:
./hw_now.sh
```

Cockpit shortcut:

```sh
./tools/dev/cockpit.sh rc
```

`run_matrix_and_smoke.sh` ensures PlatformIO caches land under `$HOME/.platformio` (via `PLATFORMIO_CORE_DIR`) rather than inside the repo.
Before smoke it shows `⚠️ BRANCHE L’USB MAINTENANT ⚠️` three times, then waits for Enter while listing ports every 15s.
Each run writes deterministic artifacts under `artifacts/rc_live/<env_label>_<timestamp>/` and logs under `logs/rc_live/<env_label>_<timestamp>.log` (`summary.json`, `summary.md`, `ports_resolve.json`, `ui_link.log`, per-step logs).
The runner resolves macOS CP2102 by LOCATION (`20-6.1.1` ESP32, `20-6.1.2` ESP8266 USB), then enforces a dedicated `UI_LINK_STATUS connected=1` gate on ESP32.
When `ZACUS_ENV="freenove_esp32s3"` (single-board), ESP8266/UI-link/story-screen gates are marked `SKIP` with `not needed for combined board`.

Environment overrides:

- `ZACUS_REQUIRE_HW=1 ./tools/dev/run_matrix_and_smoke.sh` (fail when no hardware).
- `ZACUS_WAIT_PORT=3 ./tools/dev/run_matrix_and_smoke.sh` (override serial wait window for smoke).
- `ZACUS_NO_COUNTDOWN=1 ./tools/dev/run_matrix_and_smoke.sh` (skip the USB wait gate).
- `ZACUS_SKIP_SMOKE=1 ./tools/dev/run_matrix_and_smoke.sh` (build only, no serial smoke step).
- `ZACUS_ENV="esp32dev esp8266_oled" ./tools/dev/run_matrix_and_smoke.sh` (custom env subset).
- `ZACUS_ENV="freenove_esp32s3" ./tools/dev/run_matrix_and_smoke.sh` (single-board Freenove path).
- `ZACUS_FORCE_BUILD=1 ./tools/dev/run_matrix_and_smoke.sh` (force rebuild even when artifacts exist).

By default the smoke step exits 0 when no serial hardware is present; use `ZACUS_REQUIRE_HW=1` to enforce detection.

## Docs

- Cablage ESP32/UI: `esp32_audio/WIRING.md`
- Cablage TFT: `ui/rp2040_tft/WIRING.md`
- Quickstart flash: `docs/QUICKSTART.md`
- RC board execution: `docs/RC_FINAL_BOARD.md`
- Protocole: `protocol/ui_link_v2.md`
- Cockpit command registry: `docs/_generated/COCKPIT_COMMANDS.md`

## Codex prompts

Prompt files live under `tools/dev/codex_prompts/*.prompt.md` and are designed to be consumed by the automation-friendly `codex exec` command.
Run `./tools/dev/codex_prompt_menu.sh` to see a numbered menu, pick a prompt, and send it to `codex exec --sandbox workspace-write --output-last-message artifacts/rc_live/_codex_last_message.md`.
You can also launch this helper directly from the firmware cockpit (`./tools/dev/cockpit.sh` option 6) so the existing workflow keeps a single entry point.

Story authoring prompts live separately under `docs/protocols/story_specs/prompts/*.prompt.md`. They are not ops prompts, but can still be used with Codex tooling when needed.
