# `idf_zacus/` — Zacus master ESP-IDF scaffold

This tree is the future home of the Zacus master firmware. It is the **P1
first slice** of the voice pipeline migration described in
`docs/superpowers/specs/2026-05-03-voice-pipeline-esp-sr-design.md`.

The Arduino firmware in `../ui_freenove_allinone/` keeps running unchanged
during the transition; this scaffold lives side-by-side until feature parity
is reached.

## Prerequisites

- ESP-IDF v5.4 or v5.5 installed under `~/esp/esp-idf/`.
- Source the IDF environment in each shell:
  ```bash
  . $HOME/esp/esp-idf/export.sh
  ```

## Build

```bash
cd idf_zacus
idf.py set-target esp32s3
idf.py build
```

## Flash & monitor

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Exit the monitor with `Ctrl-]`.

## What the first slice does

`main/main.c` boots the device, initializes NVS, mounts the LittleFS
`storage` partition on `/littlefs`, lists its contents, logs heap stats
(internal + PSRAM), and enters an idle heartbeat loop (no deep sleep — the
inherited `ota_server` listening loop will be wired in slice 2).

Inherited components:

- `components/ota_server/` — HTTP server on :80 with rate-limited OTA upload
  and 30 s watchdog auto-rollback (`POST /ota`, `POST /ota/rollback`,
  `GET /version`, `GET /status`, `GET /ota/status`). Not yet started by
  `main.c`; that comes next.

## Layout

```
idf_zacus/
├── CMakeLists.txt          # project entry, points EXTRA_COMPONENT_DIRS at components/
├── sdkconfig.defaults      # ESP32-S3, octal PSRAM 80 MHz, custom partitions
├── partitions.csv          # OTA layout + 2 MB LittleFS "storage"
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml   # joltwallet/littlefs ^1.14
│   └── main.c              # app_main + LittleFS mount + heartbeat
└── components/
    └── ota_server/         # inherited from 2026-04-03 IDF bootstrap
```

## Coexistence with Arduino

`../ui_freenove_allinone/` (Arduino, PlatformIO) remains the production
firmware until the IDF port reaches feature parity. The two trees do **not**
share build artifacts. To work on the Arduino tree:
`cd ui_freenove_allinone && pio run`. To work on the IDF tree, source the
ESP-IDF env first.

## Roadmap (next P1 slices)

1. Boot `ota_server_init()` from `app_main` after a small Wi-Fi STA bring-up.
2. Port the NPC engine and media manager skeleton.
3. Scaffold the voice pipeline (I2S RX task, no esp-sr yet).
4. Bring up esp-sr AFE + wakenet ("hi_esp" placeholder) — start of P3.

See the design spec for the full plan.
