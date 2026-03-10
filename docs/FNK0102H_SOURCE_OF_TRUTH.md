# FNK0102H Source Of Truth

## Cible officielle

| Élément | Valeur |
| --- | --- |
| Carte | Freenove FNK0102H |
| Module / board profile | Freenove ESP32-S3 WROOM N16R8 |
| Flash | 16 MB |
| PSRAM | 8 MB |
| Framework | Arduino |
| PlatformIO | `espressif32@6.5.0` |
| Env canonique | `freenove_esp32s3_full_with_ui` |
| `board` PlatformIO | `freenove_esp32_s3_wroom` |
| Board manifest local | `boards/freenove_esp32_s3_wroom.json` |

## Mémoire et build

| Paramètre | Valeur |
| --- | --- |
| `board_build.flash_size` | `16MB` |
| `board_upload.maximum_size` | `4194304` |
| `board_build.partitions` | `partitions/freenove_esp32s3_app4mb_fs12096kb.csv` |
| `FREENOVE_HAS_TOUCH` | `0` en release |

## Écran et entrées

| Fonction | GPIO / Paramètre |
| --- | --- |
| LCD variant | `FNK0102H_ST7796_320x480` |
| Rotation | `1` |
| TFT SCK | 47 |
| TFT MOSI | 21 |
| TFT DC | 45 |
| TFT RESET | 20 |
| TFT BL | 2 |
| Boutons 5 directions | GPIO19 via ladder analogique |

## Stockage et audio

| Fonction | GPIO |
| --- | --- |
| SD CMD | 38 |
| SD CLK | 39 |
| SD D0 | 40 |
| I2S WS | 41 |
| I2S BCK | 42 |
| I2S DOUT | 1 |
| I2S IN SCK / WS / DIN | 3 / 14 / 46 |

## Caméra

| Fonction | GPIO |
| --- | --- |
| XCLK | 15 |
| SIOD / SIOC | 4 / 5 |
| Y2 / Y3 / Y4 / Y5 / Y6 | 11 / 9 / 8 / 10 / 12 |
| Y7 / Y8 / Y9 | 18 / 17 / 16 |
| VSYNC / HREF / PCLK | 6 / 7 / 13 |
| PWDN / RESET | `-1` / `-1` |

## Hors support release

- `freenove_esp32s3_touch` est expérimental.
- Le tactile n'est pas une interface officielle du chemin release.
- Les broches `GPIO9` et `GPIO15` sont déjà utilisées par la caméra dans ce profil.
- Toute tentative d'activer le tactile doit être traitée comme une variante séparée, hors CI et hors release.

## Sources externes

- <https://docs.espressif.com/projects/arduino-esp32/en/latest/troubleshooting.html>
- <https://docs.freenove.com/projects/fnk0102/en/latest/>
- <https://freenove.com/products/fnk0102>
