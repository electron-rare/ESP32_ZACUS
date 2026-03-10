# ui_freenove_allinone

Firmware principal Freenove pour `ESP32_ZACUS`.

## Profil matériel release

- Carte: Freenove FNK0102H.
- Board profile PlatformIO: `freenove_esp32_s3_wroom` (`N16R8`, 16 MB flash / 8 MB PSRAM).
- Écran: ST7796 320x480, rotation `1`.
- Entrée officielle: switch analogique 5 directions sur GPIO19.
- Touch: non supporté dans le chemin release.

## Mapping principal

| Fonction | GPIO |
| --- | --- |
| TFT SCK | 47 |
| TFT MOSI | 21 |
| TFT DC | 45 |
| TFT RESET | 20 |
| TFT BL | 2 |
| Boutons analogiques | 19 |
| SD CMD / CLK / D0 | 38 / 39 / 40 |
| I2S WS / BCK / DOUT | 41 / 42 / 1 |
| Caméra XCLK / SIOD / SIOC | 15 / 4 / 5 |

Le détail complet est maintenu dans `../docs/FNK0102H_SOURCE_OF_TRUTH.md`.

## Build

Depuis la racine du repo:

```bash
source .venv/bin/activate
python -m platformio run -e freenove_esp32s3_full_with_ui
python -m platformio run -e freenove_esp32s3_full_with_ui -t buildfs
python -m platformio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port <PORT>
python -m platformio run -e freenove_esp32s3_full_with_ui -t upload --upload-port <PORT>
```

## Boot modes

- Normal: UI complète, caméra, audio et réseau autorisés.
- `safe diagnostic mode`: affichage minimal + boutons + série uniquement si la PSRAM n'est pas détectée.

## Tests

```bash
python tests/sprint1_utility_contract.py --mode serial --port <PORT>
python tests/sprint2_capture_contract.py --mode serial --port <PORT>
python tests/sprint3_audio_contract.py --mode serial --port <PORT>
python tests/phase9_ui_validation.py --port <PORT>
```
