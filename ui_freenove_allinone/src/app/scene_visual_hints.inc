void configureSceneVisualHardwareHints(const char* scene_id, const String& payload_json) {
  const bool is_u_son_proto = (scene_id != nullptr) && (std::strcmp(scene_id, "SCENE_U_SON_PROTO") == 0);
  const bool is_warning_scene = (scene_id != nullptr) && (std::strcmp(scene_id, "SCENE_WARNING") == 0);
  const bool is_lefou_scene = (scene_id != nullptr) && (std::strcmp(scene_id, "SCENE_LEFOU_DETECTOR") == 0);
  static constexpr uint16_t kLefouDefaultSequenceHz[] = {440U, 880U, 330U, 349U, 147U, 98U};

  SceneBacklightFxState backlight = {};
  backlight.enabled = is_u_son_proto;
  backlight.level_sync = is_u_son_proto;
  backlight.min_level = 0U;
  backlight.max_level = 95U;
  backlight.base_level = 18U;
  backlight.spike_pct = 18U;
  backlight.glitch_pct = 65U;

  SceneWs2812FxState ws = {};
  ws.single_random_blink = is_u_son_proto;
  ws.dominant_band_single = is_u_son_proto;
  ws.one_led_at_a_time = true;
  ws.level_sync = is_u_son_proto;
  ws.dynamic_color = is_u_son_proto;
  ws.r = 0x6FU;
  ws.g = 0xD8U;
  ws.b = 0xFFU;
  ws.brightness = 48U;
  ws.min_brightness = 4U;
  ws.max_brightness = 95U;

  SceneAmbientAudioFxState ambient = {};
  ambient.enabled = is_u_son_proto;
  ambient.non_interruptive = true;
  std::strncpy(ambient.mode, is_u_son_proto ? "random_file" : "single_track", sizeof(ambient.mode) - 1U);
  ambient.mode[sizeof(ambient.mode) - 1U] = '\0';
  std::strncpy(ambient.track, kUsonAmbientTrack, sizeof(ambient.track) - 1U);
  ambient.track[sizeof(ambient.track) - 1U] = '\0';
  std::strncpy(ambient.playlist_root, is_u_son_proto ? "/music/v8_pack" : "", sizeof(ambient.playlist_root) - 1U);
  ambient.playlist_root[sizeof(ambient.playlist_root) - 1U] = '\0';
  ambient.playlist_recursive = true;
  uint16_t lefou_sequence_hz[RuntimeHardwareConfig::kLaSequenceMaxNotes] = {};
  uint8_t lefou_sequence_count = 0U;
  uint16_t lefou_note_hold_ms = 350U;
  bool warning_siren = is_warning_scene;
  if (is_lefou_scene) {
    lefou_sequence_count = static_cast<uint8_t>(sizeof(kLefouDefaultSequenceHz) / sizeof(kLefouDefaultSequenceHz[0]));
    if (lefou_sequence_count > RuntimeHardwareConfig::kLaSequenceMaxNotes) {
      lefou_sequence_count = RuntimeHardwareConfig::kLaSequenceMaxNotes;
    }
    for (uint8_t idx = 0U; idx < lefou_sequence_count; ++idx) {
      lefou_sequence_hz[idx] = kLefouDefaultSequenceHz[idx];
    }
  }

  uint32_t accent_rgb = 0x6FD8FFUL;
  bool ws_use_theme_accent = ws.single_random_blink;

  if (!payload_json.isEmpty()) {
    DynamicJsonDocument document(4096);
    const DeserializationError error = deserializeJson(document, payload_json);
    if (!error) {
      parseHexRgbColor(document["theme"]["accent"] | document["visual"]["theme"]["accent"] | "", &accent_rgb);

      if (document["text"]["glitch"].is<unsigned int>()) {
        backlight.glitch_pct = static_cast<uint8_t>(document["text"]["glitch"].as<unsigned int>());
      } else if (document["text"]["glitch_pct"].is<unsigned int>()) {
        backlight.glitch_pct = static_cast<uint8_t>(document["text"]["glitch_pct"].as<unsigned int>());
      }
      if (backlight.glitch_pct > 100U) {
        backlight.glitch_pct = 100U;
      }

      const char* backlight_mode = document["hardware"]["backlight"]["mode"] | "";
      if (backlight_mode[0] != '\0') {
        if (std::strcmp(backlight_mode, "level_sync") == 0 || std::strcmp(backlight_mode, "level") == 0) {
          backlight.enabled = true;
          backlight.level_sync = true;
        } else if (std::strcmp(backlight_mode, "glitch_sync") == 0 || std::strcmp(backlight_mode, "glitch") == 0) {
          backlight.enabled = true;
          backlight.level_sync = false;
        } else {
          backlight.enabled = false;
          backlight.level_sync = false;
        }
      }
      if (document["hardware"]["backlight"]["min"].is<int>()) {
        const int parsed = document["hardware"]["backlight"]["min"].as<int>();
        backlight.min_level = static_cast<uint8_t>((parsed < 0) ? 0 : ((parsed > 125) ? 125 : parsed));
      }
      if (document["hardware"]["backlight"]["max"].is<int>()) {
        const int parsed = document["hardware"]["backlight"]["max"].as<int>();
        backlight.max_level = static_cast<uint8_t>((parsed < 0) ? 0 : ((parsed > 125) ? 125 : parsed));
      }
      if (backlight.min_level > backlight.max_level) {
        const uint8_t swapped = backlight.min_level;
        backlight.min_level = backlight.max_level;
        backlight.max_level = swapped;
      }
      if (document["hardware"]["backlight"]["base"].is<int>()) {
        const int parsed = document["hardware"]["backlight"]["base"].as<int>();
        backlight.base_level = static_cast<uint8_t>((parsed < 0) ? 0 : ((parsed > 125) ? 125 : parsed));
      }
      if (backlight.base_level < backlight.min_level) {
        backlight.base_level = backlight.min_level;
      } else if (backlight.base_level > backlight.max_level) {
        backlight.base_level = backlight.max_level;
      }
      if (document["hardware"]["backlight"]["spike_pct"].is<unsigned int>()) {
        backlight.spike_pct = static_cast<uint8_t>(document["hardware"]["backlight"]["spike_pct"].as<unsigned int>());
      }
      if (backlight.spike_pct > 100U) {
        backlight.spike_pct = 100U;
      }

      const char* ws_mode = document["hardware"]["ws2812"]["mode"] | "";
      if (ws_mode[0] != '\0') {
        if (std::strcmp(ws_mode, "single_random_blink") == 0) {
          ws.single_random_blink = true;
          ws.dominant_band_single = false;
        } else if (std::strcmp(ws_mode, "dominant_band_single") == 0 ||
                   std::strcmp(ws_mode, "single_band_dominant") == 0) {
          ws.single_random_blink = true;
          ws.dominant_band_single = true;
        } else {
          ws.single_random_blink = false;
          ws.dominant_band_single = false;
        }
      }
      if (document["hardware"]["ws2812"]["level_sync"].is<bool>()) {
        ws.level_sync = document["hardware"]["ws2812"]["level_sync"].as<bool>();
      }
      if (document["hardware"]["ws2812"]["one_led_at_a_time"].is<bool>()) {
        ws.one_led_at_a_time = document["hardware"]["ws2812"]["one_led_at_a_time"].as<bool>();
      }
      const char* ws_color_source = document["hardware"]["ws2812"]["color_source"] | "";
      if (ws_color_source[0] != '\0') {
        if (std::strcmp(ws_color_source, "theme.accent") == 0) {
          ws_use_theme_accent = true;
          ws.dynamic_color = false;
        } else if (std::strcmp(ws_color_source, "mic.dynamic") == 0 ||
                   std::strcmp(ws_color_source, "mic") == 0) {
          ws.dynamic_color = true;
          ws_use_theme_accent = false;
        }
      }
      if (document["hardware"]["ws2812"]["dynamic_color"].is<bool>()) {
        ws.dynamic_color = document["hardware"]["ws2812"]["dynamic_color"].as<bool>();
      }
      uint32_t ws_color_rgb = accent_rgb;
      if (parseHexRgbColor(document["hardware"]["ws2812"]["color"] | "", &ws_color_rgb)) {
        ws_use_theme_accent = false;
        ws.dynamic_color = false;
      }
      if (document["hardware"]["ws2812"]["brightness"].is<unsigned int>()) {
        ws.brightness = static_cast<uint8_t>(document["hardware"]["ws2812"]["brightness"].as<unsigned int>());
      }
      if (ws.brightness > 125U) {
        ws.brightness = 125U;
      }
      if (document["hardware"]["ws2812"]["min_brightness"].is<unsigned int>()) {
        ws.min_brightness = static_cast<uint8_t>(document["hardware"]["ws2812"]["min_brightness"].as<unsigned int>());
      }
      if (document["hardware"]["ws2812"]["max_brightness"].is<unsigned int>()) {
        ws.max_brightness = static_cast<uint8_t>(document["hardware"]["ws2812"]["max_brightness"].as<unsigned int>());
      }
      if (ws.max_brightness > 125U) {
        ws.max_brightness = 125U;
      }
      if (ws.min_brightness > ws.max_brightness) {
        ws.min_brightness = ws.max_brightness;
      }
      if (ws_use_theme_accent) {
        ws_color_rgb = accent_rgb;
      }
      ws.r = static_cast<uint8_t>((ws_color_rgb >> 16U) & 0xFFU);
      ws.g = static_cast<uint8_t>((ws_color_rgb >> 8U) & 0xFFU);
      ws.b = static_cast<uint8_t>(ws_color_rgb & 0xFFU);

      if (document["audio"]["ambient_random"].is<JsonObjectConst>()) {
        const JsonObjectConst ambient_cfg = document["audio"]["ambient_random"].as<JsonObjectConst>();
        if (ambient_cfg["enabled"].is<bool>()) {
          ambient.enabled = ambient_cfg["enabled"].as<bool>();
        }
        if (ambient_cfg["non_interruptive"].is<bool>()) {
          ambient.non_interruptive = ambient_cfg["non_interruptive"].as<bool>();
        }
        const char* mode = ambient_cfg["mode"] | "";
        if (mode[0] != '\0') {
          std::strncpy(ambient.mode, mode, sizeof(ambient.mode) - 1U);
          ambient.mode[sizeof(ambient.mode) - 1U] = '\0';
        }
        const char* track = ambient_cfg["track"] | "";
        if (track[0] != '\0') {
          std::strncpy(ambient.track, track, sizeof(ambient.track) - 1U);
          ambient.track[sizeof(ambient.track) - 1U] = '\0';
        }
        const char* playlist_root = ambient_cfg["playlist_root"] | "";
        if (playlist_root[0] != '\0') {
          std::strncpy(ambient.playlist_root, playlist_root, sizeof(ambient.playlist_root) - 1U);
          ambient.playlist_root[sizeof(ambient.playlist_root) - 1U] = '\0';
        }
        if (ambient_cfg["playlist_recursive"].is<bool>()) {
          ambient.playlist_recursive = ambient_cfg["playlist_recursive"].as<bool>();
        }
        if (ambient_cfg["volume_min"].is<unsigned int>()) {
          uint32_t parsed = ambient_cfg["volume_min"].as<uint32_t>();
          if (parsed > FREENOVE_AUDIO_MAX_VOLUME) {
            parsed = FREENOVE_AUDIO_MAX_VOLUME;
          }
          ambient.volume_min = static_cast<uint8_t>(parsed);
        }
        if (ambient_cfg["volume_max"].is<unsigned int>()) {
          uint32_t parsed = ambient_cfg["volume_max"].as<uint32_t>();
          if (parsed > FREENOVE_AUDIO_MAX_VOLUME) {
            parsed = FREENOVE_AUDIO_MAX_VOLUME;
          }
          ambient.volume_max = static_cast<uint8_t>(parsed);
        }
        if (ambient_cfg["delay_min_ms"].is<unsigned int>()) {
          ambient.delay_min_ms = ambient_cfg["delay_min_ms"].as<uint32_t>();
        }
        if (ambient_cfg["delay_max_ms"].is<unsigned int>()) {
          ambient.delay_max_ms = ambient_cfg["delay_max_ms"].as<uint32_t>();
        }
      }

      if (is_lefou_scene && document["render"]["lefou_detector"].is<JsonObjectConst>()) {
        const JsonObjectConst lefou_render = document["render"]["lefou_detector"].as<JsonObjectConst>();
        if (lefou_render["note_sequence_hz"].is<JsonArrayConst>()) {
          const JsonArrayConst note_sequence = lefou_render["note_sequence_hz"].as<JsonArrayConst>();
          uint8_t note_count = 0U;
          for (uint8_t idx = 0U; idx < RuntimeHardwareConfig::kLaSequenceMaxNotes; ++idx) {
            lefou_sequence_hz[idx] = 0U;
          }
          for (JsonVariantConst note_node : note_sequence) {
            if (!note_node.is<unsigned int>()) {
              continue;
            }
            const uint32_t parsed_hz = note_node.as<uint32_t>();
            if (parsed_hz < 40U || parsed_hz > 3000U) {
              continue;
            }
            if (note_count >= RuntimeHardwareConfig::kLaSequenceMaxNotes) {
              break;
            }
            lefou_sequence_hz[note_count++] = static_cast<uint16_t>(parsed_hz);
          }
          lefou_sequence_count = note_count;
        }
        if (lefou_render["note_hold_ms"].is<unsigned int>()) {
          lefou_note_hold_ms = static_cast<uint16_t>(lefou_render["note_hold_ms"].as<uint32_t>());
        }
      }
      if (document["render"]["warning"]["siren"].is<bool>()) {
        warning_siren = document["render"]["warning"]["siren"].as<bool>();
      }
    }
  }

  if (ws.single_random_blink) {
    ws.one_led_at_a_time = true;
  }
  if (ws.level_sync && !ws.single_random_blink) {
    ws.level_sync = false;
  }
  if (backlight.level_sync) {
    if (backlight.max_level > 99U) {
      backlight.max_level = 99U;
    }
    if (is_u_son_proto && backlight.min_level < 30U) {
      backlight.min_level = 30U;
    }
    if (is_u_son_proto && backlight.base_level < 30U) {
      backlight.base_level = 30U;
    }
    if (backlight.base_level > backlight.max_level) {
      backlight.base_level = backlight.max_level;
    }
    if (backlight.min_level > backlight.max_level) {
      backlight.min_level = backlight.max_level;
    }
    if (backlight.base_level < backlight.min_level) {
      backlight.base_level = backlight.min_level;
    }
  }
  if (ws.level_sync) {
    if (ws.max_brightness > 99U) {
      ws.max_brightness = 99U;
    }
    if (ws.min_brightness > ws.max_brightness) {
      ws.min_brightness = ws.max_brightness;
    }
    if (ws.brightness < ws.min_brightness) {
      ws.brightness = ws.min_brightness;
    } else if (ws.brightness > ws.max_brightness) {
      ws.brightness = ws.max_brightness;
    }
  }
  if (ambient.volume_min > FREENOVE_AUDIO_MAX_VOLUME) {
    ambient.volume_min = FREENOVE_AUDIO_MAX_VOLUME;
  }
  if (ambient.volume_max > FREENOVE_AUDIO_MAX_VOLUME) {
    ambient.volume_max = FREENOVE_AUDIO_MAX_VOLUME;
  }
  if (ambient.volume_min > ambient.volume_max) {
    ambient.volume_min = ambient.volume_max;
  }
  {
    String mode = ambient.mode;
    mode.trim();
    mode.toLowerCase();
    if (mode.isEmpty()) {
      mode = "single_track";
    }
    std::strncpy(ambient.mode, mode.c_str(), sizeof(ambient.mode) - 1U);
    ambient.mode[sizeof(ambient.mode) - 1U] = '\0';
  }
  if (is_u_son_proto) {
    ambient.enabled = true;
    ambient.non_interruptive = true;
    ambient.playlist_recursive = true;
    if (ambient.track[0] == '\0') {
      std::strncpy(ambient.track, kUsonAmbientTrack, sizeof(ambient.track) - 1U);
      ambient.track[sizeof(ambient.track) - 1U] = '\0';
    }
    if (!isAmbientRandomFileModeToken(ambient.mode)) {
      std::strncpy(ambient.mode, "random_file", sizeof(ambient.mode) - 1U);
      ambient.mode[sizeof(ambient.mode) - 1U] = '\0';
    }
    if (ambient.playlist_root[0] == '\0') {
      std::strncpy(ambient.playlist_root, "/music/v8_pack", sizeof(ambient.playlist_root) - 1U);
      ambient.playlist_root[sizeof(ambient.playlist_root) - 1U] = '\0';
    }
    ambient.volume_min = kUsonAmbientVolumeMin;
    ambient.volume_max = kUsonAmbientVolumeMax;
    ambient.delay_min_ms = kUsonAmbientDelayMinMs;
    ambient.delay_max_ms = kUsonAmbientDelayMaxMs;
  }
  if (ambient.delay_min_ms < 1000U) {
    ambient.delay_min_ms = 1000U;
  }
  if (ambient.delay_max_ms < ambient.delay_min_ms) {
    ambient.delay_max_ms = ambient.delay_min_ms;
  }
  if (ambient.delay_max_ms > kUsonAmbientDelayMaxMs) {
    ambient.delay_max_ms = kUsonAmbientDelayMaxMs;
  }
  if (!isAmbientRandomFileModeToken(ambient.mode)) {
    ambient.playlist_root[0] = '\0';
  }

  g_hardware_cfg.mic_la_sequence_enabled = false;
  g_hardware_cfg.mic_la_sequence_count = 0U;
  g_hardware_cfg.mic_la_sequence_note_hold_ms = 350U;
  for (uint8_t idx = 0U; idx < RuntimeHardwareConfig::kLaSequenceMaxNotes; ++idx) {
    g_hardware_cfg.mic_la_sequence_hz[idx] = 0U;
  }
  if (is_lefou_scene) {
    g_hardware_cfg.mic_la_timeout_ms = 0U;
    if (lefou_note_hold_ms < 100U) {
      lefou_note_hold_ms = 100U;
    } else if (lefou_note_hold_ms > 3000U) {
      lefou_note_hold_ms = 3000U;
    }
    g_hardware_cfg.mic_la_sequence_note_hold_ms = lefou_note_hold_ms;
    if (lefou_sequence_count > RuntimeHardwareConfig::kLaSequenceMaxNotes) {
      lefou_sequence_count = RuntimeHardwareConfig::kLaSequenceMaxNotes;
    }
    g_hardware_cfg.mic_la_sequence_count = lefou_sequence_count;
    g_hardware_cfg.mic_la_sequence_enabled = (lefou_sequence_count > 0U);
    for (uint8_t idx = 0U; idx < lefou_sequence_count; ++idx) {
      g_hardware_cfg.mic_la_sequence_hz[idx] = lefou_sequence_hz[idx];
    }
    if (g_hardware_cfg.mic_la_sequence_count > 0U) {
      g_hardware_cfg.mic_la_target_hz = g_hardware_cfg.mic_la_sequence_hz[0];
    }
  } else if (g_hardware_cfg.mic_la_timeout_ms == 0U) {
    // SCENE_LEFOU disables timeout; restore the default timeout on other scenes.
    g_hardware_cfg.mic_la_timeout_ms = RuntimeHardwareConfig().mic_la_timeout_ms;
  }

  g_scene_backlight_fx = backlight;
  g_scene_ws2812_fx = ws;
  g_scene_ambient_audio_fx = ambient;
  g_scene_warning_siren_fx.enabled = is_warning_scene && warning_siren;
  if (!g_scene_warning_siren_fx.enabled) {
    g_scene_warning_siren_fx.local_fallback_logged = false;
  }
  g_scene_ws2812_runtime_brightness = ws.brightness;
  g_scene_ws2812_runtime_next_update_ms = 0U;
  const char* backlight_mode = "static";
  if (g_scene_backlight_fx.enabled) {
    backlight_mode = g_scene_backlight_fx.level_sync ? "level_sync" : "glitch_sync";
  }
  std::strncpy(g_scene_backlight_mode,
               backlight_mode,
               sizeof(g_scene_backlight_mode) - 1U);
  g_scene_backlight_mode[sizeof(g_scene_backlight_mode) - 1U] = '\0';
  g_hardware.setSceneSingleRandomBlink(g_scene_ws2812_fx.single_random_blink,
                                       g_scene_ws2812_fx.r,
                                       g_scene_ws2812_fx.g,
                                       g_scene_ws2812_fx.b,
                                       g_scene_ws2812_runtime_brightness,
                                       g_scene_ws2812_fx.dominant_band_single);
}
