# ESP32_ZACUS – Freenove ESP32-S3 UI Firmware

Branche de travail dédiée pour optimisations et refactoring de l'interface Freenove.

**Status**: 2026-03-01 – Repo initialisé avec sources Freenove + story engine library.

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

### Build & Upload Freenove

```bash
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
