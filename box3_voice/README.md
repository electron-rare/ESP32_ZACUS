# Zacus BOX-3 Voice Pipeline

ESP-IDF firmware for ESP32-S3-BOX-3 — voice interface for
*Le Mystere du Professeur Zacus* escape room.

## Prerequisites

- ESP-IDF v5.2+ (with ESP32-S3 support)
- Python 3.8+ (for idf.py)

## Build

```bash
# Set target
idf.py set-target esp32s3

# Configure (optional — defaults in sdkconfig.defaults)
idf.py menuconfig

# Build
idf.py build

# Flash (USB-C on BOX-3)
idf.py -p /dev/tty.usbmodem* flash monitor
```

## Configuration

Use `idf.py menuconfig` > **Zacus BOX-3 Voice Configuration**:

| Option | Default | Description |
|--------|---------|-------------|
| WiFi SSID | `zacus-net` | Network for voice bridge |
| WiFi Password | *(empty)* | WPA2 password |
| Voice Bridge URL | `ws://192.168.4.1:8080/ws/voice` | mascarade WebSocket |
| Wake Word | `hi esp` | WakeNet9 trigger phrase |
| Speaker Volume | 70 | 0-100 |
| Mic Gain | 80 | 0-100 |

## Hardware

- **Board**: ESP32-S3-BOX-3 (16MB flash, 8MB PSRAM)
- **Codec**: ES8311 (I2C addr 0x18)
- **Display**: ILI9341 320x240
- **Mic**: onboard MEMS
- **Speaker**: onboard 1W with PA

## Architecture

```
[Mic] -> I2S -> WakeNet -> Voice Capture -> WebSocket -> mascarade bridge
                                                              |
[Speaker] <- I2S <- TTS audio <------------- WebSocket -------+
                                                              |
                                              hints engine <--+
```
