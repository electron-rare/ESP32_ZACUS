# Specs Applications Zacus

Specs de développement par application, alignées sur le runtime actuel (`AppRegistry`, `AppRuntimeManager`, `app_modules.cpp`).

## Contrat plateforme (commun)
- Ouverture app: `POST /api/apps/open` ou commande série `APP_OPEN <id> [mode]`.
- Fermeture app: `POST /api/apps/close` ou commande série `APP_CLOSE [reason]`.
- Action app: `POST /api/apps/action` ou commande série `APP_ACTION <action> [payload]`.
- Le runtime injecte la scène `entry_screen` dans UiManager au démarrage app.
- Si `required_capabilities` non disponibles: erreur `resource_busy`.
- Si `asset_manifest` manquant et app offline: erreur `missing_asset`.

## Launcher / Workbench
- [Grille d'Apps type Workbench Amiga](./workbench_amiga_grid.md)

## Applications
- [Lecteur Audio (`audio_player`)](./audio_player.md) - module `AudioPlayerModule`, scène `SCENE_AUDIO_PLAYER`
- [Calculatrice (`calculator`)](./calculator.md) - module `CalculatorModule`, scène `SCENE_CALCULATOR`
- [Chronometre/Minuteur (`timer_tools`)](./timer_tools.md) - module `TimerToolsModule`, scène `SCENE_TIMER`
- [Lampe de Poche (`flashlight`)](./flashlight.md) - module `FlashlightModule`, scène `SCENE_FLASHLIGHT`
- [Appareil Photo/Video (`camera_video`)](./camera_video.md) - module `CameraVideoModule`, scène `SCENE_PHOTO_MANAGER`
- [Dictaphone (`dictaphone`)](./dictaphone.md) - module `DictaphoneModule`, scène `SCENE_RECORDER`
- [Lecteur QR Code (`qr_scanner`)](./qr_scanner.md) - module `QrScannerModule`, scène `SCENE_QR_DETECTOR`
- [Livres Audio (`audiobook_player`)](./audiobook_player.md) - module `AudiobookModule`, scène `SCENE_AUDIOBOOK`
- [Webradio Enfants (`kids_webradio`)](./kids_webradio.md) - module `AudioPlayerModule`, scène `SCENE_WEBRADIO`
- [Podcast Enfants (`kids_podcast`)](./kids_podcast.md) - module `AudioPlayerModule`, scène `SCENE_PODCAST`
- [Musique Enfants (`kids_music`)](./kids_music.md) - module `AudioPlayerModule`, scène `SCENE_KIDS_MUSIC`
- [Yoga Enfants (`kids_yoga`)](./kids_yoga.md) - module `KidsLearningModule`, scène `SCENE_KIDS_YOGA`
- [Meditation Enfants (`kids_meditation`)](./kids_meditation.md) - module `KidsLearningModule`, scène `SCENE_KIDS_MEDITATION`
- [Langues Enfants (`kids_languages`)](./kids_languages.md) - module `KidsLearningModule`, scène `SCENE_KIDS_LANG`
- [Maths Enfants (`kids_math`)](./kids_math.md) - module `KidsLearningModule`, scène `SCENE_KIDS_MATH`
- [Sciences Enfants (`kids_science`)](./kids_science.md) - module `KidsLearningModule`, scène `SCENE_KIDS_SCIENCE`
- [Geographie Enfants (`kids_geography`)](./kids_geography.md) - module `KidsLearningModule`, scène `SCENE_KIDS_GEO`
- [Dessin (`kids_drawing`)](./kids_drawing.md) - module `KidsCreativeModule`, scène `SCENE_DRAWING`
- [Coloriage (`kids_coloring`)](./kids_coloring.md) - module `KidsCreativeModule`, scène `SCENE_COLORING`
- [Emulateur NES (`nes_emulator`)](./nes_emulator.md) - module `NesEmulatorModule`, scène `SCENE_NES_EMU`
