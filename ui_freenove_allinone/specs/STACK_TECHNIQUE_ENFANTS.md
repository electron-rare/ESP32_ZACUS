# Stack Technique Enfants (ESP32-S3)

## Socle
- `MCU`: ESP32-S3 (OS embarqué + orchestration apps/services)
- `UI`: LVGL + shell enfant inspiré Amiga
- `Audio`: lecture locale/streaming + dictaphone
- `Capture`: caméra/QR
- `Réseau`: Wi-Fi + ESP-NOW + Bonjour/mDNS

## Partage Bonjour + fichiers
- Découverte locale via `mDNS` service `_zacus._tcp`
- API de découverte: `GET /api/share/peers`
- API de listing fichiers partagés: `GET /api/share/files`
- API d’upload simple: `POST /api/share/upload`

## Contrats apps
- Registre apps: `/apps/registry.json` (LittleFS prioritaire, fallback embarqué)
- Manifests apps: `/apps/<app_id>/manifest.json`
- Streams apps: `/apps/<app_id>/streams.json`
- Progression apps: `/apps/<app_id>/progress.json`

## UX enfants
- Icônes colorées, feedback immédiat, texte court.
- Actions principales en 1-2 interactions.
- Animations ludiques non bloquantes (budget frame conservé).

