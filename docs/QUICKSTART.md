# Quickstart

## Prérequis

- `python3`
- accès USB vers la carte Freenove
- réseau local optionnel pour les tests HTTP

## Bootstrap

```bash
./scripts/bootstrap_platformio.sh
./scripts/doctor_repo.sh
source .venv/bin/activate
```

Le bootstrap crée une `.venv` locale et installe `platformio`, `esptool` et `pyserial`.

## Build

```bash
python -m platformio run -e freenove_esp32s3_full_with_ui
python -m platformio run -e freenove_esp32s3_full_with_ui -t buildfs
```

## Flash

```bash
python -m platformio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port <PORT>
python -m platformio run -e freenove_esp32s3_full_with_ui -t upload --upload-port <PORT>
```

## Monitor

```bash
python -m platformio device monitor -b 115200 --port <PORT>
```

## Signatures attendues

Boot normal:

```text
[MAIN] Freenove all-in-one boot
[MEM] psram_found=1
```

Boot diagnostic sans PSRAM:

```text
[MEM] PSRAM expected by build flags but not detected
[BOOT] safe diagnostic mode enabled: PSRAM required, app stack disabled
[SAFE] boot path: storage + serial + display + buttons only
```

## Smoke minimal

```bash
python tests/sprint1_utility_contract.py --mode serial --port <PORT>
python tests/sprint2_capture_contract.py --mode serial --port <PORT>
python tests/sprint3_audio_contract.py --mode serial --port <PORT>
python tests/phase9_ui_validation.py --port <PORT>
```

## Procédure si la PSRAM est absente

1. Garder le monitor série ouvert.
2. Vérifier que le boot reste vivant en mode diagnostic.
3. Confirmer l'absence d'initialisation Wi-Fi, ESP-NOW, caméra et audio.
4. Vérifier le module réel et la config `platformio.ini` avant de reflasher.
