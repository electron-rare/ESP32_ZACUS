// default_app_registry.h - Embedded fallback app registry JSON.
#pragma once

static const char kDefaultAppRegistryJson[] = R"JSON({
  "apps": [
    {"id": "audio_player",     "title": "Lecteur Audio",    "category": "media",    "entry_screen": "SCENE_AUDIO_PLAYER",  "icon_path": "/apps/icons/audio.bin",    "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_STORAGE_FS","CAP_GPU_UI"]},
    {"id": "camera_video",     "title": "Photo / Video",    "category": "capture",  "entry_screen": "SCENE_PHOTO_MANAGER", "icon_path": "/apps/icons/camera.bin",   "enabled": true, "required_capabilities": ["CAP_CAMERA","CAP_STORAGE_FS","CAP_GPU_UI"]},
    {"id": "dictaphone",       "title": "Dictaphone",       "category": "capture",  "entry_screen": "SCENE_RECORDER",      "icon_path": "/apps/icons/mic.bin",      "enabled": true, "required_capabilities": ["CAP_AUDIO_IN","CAP_STORAGE_FS","CAP_GPU_UI"]},
    {"id": "qr_scanner",       "title": "QR Scanner",       "category": "utility",  "entry_screen": "SCENE_QR_DETECTOR",   "icon_path": "/apps/icons/qr.bin",       "enabled": true, "required_capabilities": ["CAP_CAMERA","CAP_GPU_UI"]},
    {"id": "calculator",       "title": "Calculatrice",     "category": "utility",  "entry_screen": "SCENE_CALCULATOR",    "icon_path": "/apps/icons/calc.bin",     "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "timer_tools",      "title": "Chrono / Timer",   "category": "utility",  "entry_screen": "SCENE_TIMER",         "icon_path": "/apps/icons/timer.bin",    "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "flashlight",       "title": "Lampe",            "category": "utility",  "entry_screen": "SCENE_FLASHLIGHT",    "icon_path": "/apps/icons/flash.bin",    "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "audiobook_player", "title": "Livres Audio",     "category": "media",    "entry_screen": "SCENE_AUDIOBOOK",     "icon_path": "/apps/icons/book.bin",     "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_STORAGE_FS","CAP_GPU_UI"]},
    {"id": "kids_webradio",    "title": "Webradio",         "category": "kids",     "entry_screen": "SCENE_WEBRADIO",      "icon_path": "/apps/icons/radio.bin",    "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_WIFI","CAP_GPU_UI"]},
    {"id": "kids_podcast",     "title": "Podcasts",         "category": "kids",     "entry_screen": "SCENE_PODCAST",       "icon_path": "/apps/icons/podcast.bin",  "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_WIFI","CAP_GPU_UI"]},
    {"id": "kids_music",       "title": "Musique",          "category": "kids",     "entry_screen": "SCENE_KIDS_MUSIC",    "icon_path": "/apps/icons/music.bin",    "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_STORAGE_FS","CAP_GPU_UI"]},
    {"id": "kids_yoga",        "title": "Yoga",             "category": "kids",     "entry_screen": "SCENE_KIDS_YOGA",     "icon_path": "/apps/icons/yoga.bin",     "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_GPU_UI"]},
    {"id": "kids_meditation",  "title": "Meditation",       "category": "kids",     "entry_screen": "SCENE_KIDS_MEDITATION","icon_path": "/apps/icons/meditate.bin","enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_GPU_UI"]},
    {"id": "kids_languages",   "title": "Langues",          "category": "kids",     "entry_screen": "SCENE_KIDS_LANG",     "icon_path": "/apps/icons/lang.bin",     "enabled": true, "required_capabilities": ["CAP_AUDIO_OUT","CAP_GPU_UI"]},
    {"id": "kids_math",        "title": "Maths",            "category": "kids",     "entry_screen": "SCENE_KIDS_MATH",     "icon_path": "/apps/icons/math.bin",     "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "kids_science",     "title": "Sciences",         "category": "kids",     "entry_screen": "SCENE_KIDS_SCIENCE",  "icon_path": "/apps/icons/science.bin",  "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "kids_geography",   "title": "Geographie",       "category": "kids",     "entry_screen": "SCENE_KIDS_GEO",      "icon_path": "/apps/icons/geo.bin",      "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "kids_drawing",     "title": "Dessin",           "category": "kids",     "entry_screen": "SCENE_DRAWING",       "icon_path": "/apps/icons/draw.bin",     "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "kids_coloring",    "title": "Coloriage",        "category": "kids",     "entry_screen": "SCENE_COLORING",      "icon_path": "/apps/icons/color.bin",    "enabled": true, "required_capabilities": ["CAP_GPU_UI"]},
    {"id": "nes_emulator",     "title": "NES Emu",          "category": "emulator", "entry_screen": "SCENE_NES_EMU",       "icon_path": "/apps/icons/nes.bin",      "enabled": true, "required_capabilities": ["CAP_GPU_UI","CAP_STORAGE_FS"]}
  ]
})JSON";
