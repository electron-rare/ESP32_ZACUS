# ESP32_ZACUS Runtime Notes

Ce fichier résume le comportement de boot attendu sur la branche de stabilisation Freenove.

## État courant

- Toolchain figée sur `espressif32@6.5.0`.
- Cible officielle: Freenove FNK0102H / `freenove_esp32_s3_wroom` (`N16R8`, 16 MB flash / 8 MB PSRAM).
- Le tactile reste expérimental et hors chemin release.
- L'absence de PSRAM ne lance plus le shell applicatif complet.

## Signatures série attendues

Boot normal:

```text
[MAIN] Freenove all-in-one boot
[MEM] psram_found=1
[APP] registry loaded=1 ...
```

Boot diagnostic sans PSRAM:

```text
[MEM] PSRAM expected by build flags but not detected
[BOOT] safe diagnostic mode enabled: PSRAM required, app stack disabled
[SAFE] boot path: storage + serial + display + buttons only
```

## Commandes série utiles

Mode diagnostic:

- `PING`
- `HELP`
- `STATUS`
- `BTN_READ`
- `LCD_BACKLIGHT 120`

Mode normal:

- `STATUS`
- `APP_STATUS`
- `UI_MEM_STATUS`
- `NET_STATUS`
- `CAM_STATUS`

## Debug PSRAM

1. Lancer `./scripts/doctor_repo.sh`.
2. Flasher `freenove_esp32s3_full_with_ui`.
3. Ouvrir le monitor série à `115200`.
4. Si le mode diagnostic apparaît, vérifier le module réel, le mode flash/PSRAM et le câblage.
