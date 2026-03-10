# ESP32_ZACUS – Freenove ESP32-S3 UI Firmware

Branche de travail dédiée pour optimisations et refactoring de l'interface Freenove.

**Status**: 2026-03-01 – MVP apps/assets intégrés, build + flash OK, runtime série en reboot loop (debug en cours).

## Intégration en place (MVP/V1 groundwork)

- Runtime apps: registre + runtime manager + modules MVP/V1 branchés.
- NES core MVP: module `nes_emulator` (validation iNES mapper 0 + actions runtime).
- ROMs NES en LittleFS: 5 homebrews dans `data/apps/nes_emulator/roms/`.
- Assets FS: icônes PNG + SFX WAV + médias fallback MP3 générés dans `data/apps/**`.
- WebUI assets: header/favicon/sfx + fontes locales.
- Fontes WebUI: `PressStart2P` + `ComicNeue` servies depuis `LittleFS` (`/webui/assets/fonts/*.ttf`).
- PlatformIO: hook pre-build pour provisionner les fontes (`scripts/pio_prepare_webui_fonts.py`).

## Structure

```
ESP32_ZACUS/
├── ui_freenove_allinone/          # Freenove UI app (UI rendering, scenes, story integration)
├── lib/                            # Story engine + auxiliary libraries
├── platformio.ini                  # PlatformIO config (Freenove environment)
└── README.md                       # General Zacus firmware guide
```

## Workflow

### Setup Local

```bash
cd ESP32_ZACUS
python3 -m venv .venv
source .venv/bin/activate
pip install platformio esptool pyserial

# List PlatformIO boards
pio boards | grep freenove
```

### Build & Upload Freenove (firmware + LittleFS)

```bash
pio run -e freenove_esp32s3_full_with_ui
pio run -e freenove_esp32s3_full_with_ui -t buildfs
pio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port /dev/cu.usbmodem5AB90753301
pio run -e freenove_esp32s3_full_with_ui -t upload --upload-port /dev/cu.usbmodem5AB90753301
```

### Serial Monitor (115200 baud)

```bash
pio device monitor -p /dev/cu.usbmodem5AB90753301 -b 115200
```

Or Python:

```bash
python3 << 'PY'
import serial, time
port = '/dev/cu.usbmodem5AB90753301'
ser = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
ser.write(b'STATUS\n')
time.sleep(0.3)
for _ in range(10):
    line = ser.readline().decode('utf-8', 'ignore').strip()
    if line:
        print(line)
ser.close()
PY
```

## Validation du 2026-03-01

- `build` firmware: **OK**.
- `buildfs`: **OK**.
- `uploadfs`: **OK**.
- `upload` firmware: **OK**.
- test série: **KO** (reboot loop immédiat).

Extrait série observé:

```text
rst:0x3 (RTC_SW_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
Saved PC:0x403cdb0a
```

## TODO immédiat (debug runtime)

- Capturer un backtrace exploitable (désactiver reset auto/activer logs très tôt).
- Vérifier cohérence board/flash/PSRAM par rapport au hardware réel.
- Isoler les init précoces (PSRAM, display/camera/audio) pour trouver la séquence qui déclenche `esp_restart`.
- Refaire smoke série après correctif (boot stable + init runtime + endpoints web).

## Future Work

- **UI Optimization** (ui_freenove_allinone):
  - Refactor LVGL rendering pipeline
  - Scene/story integration improvements
  - Touch input handling; audio trigger feedback
  
- **Story Engine** (lib/story/):
  - Performance audit
  - Memory footprint reduction
  - ESP-NOW sync protocol optimization

- **Hardware Integration**:
  - Audio codec I2S optimization
  - PSRAM buffer management
  - Camera/WiFi/Bluetooth coexistence

## Git Push to GitHub

Once repo created on GitHub:

```bash
cd ESP32_ZACUS
git remote add origin https://github.com/YOUR_USER/ESP32_ZACUS.git
git branch -m master main
git push -u origin main
```

## References

- [Freenove Board Docs](../hardware/docs/RC_FINAL_BOARD.md)
- [Story Engine Contract](../hardware/firmware/docs/ESP_NOW_API_CONTRACT_FREENOVE_V1.md)
- [Zacus Game Protocol](../hardware/firmware/docs/ARCHITECTURE_UML.md)

---

**Last Updated**: 2026-03-01  
**Authored by**: Agent (Copilot)
