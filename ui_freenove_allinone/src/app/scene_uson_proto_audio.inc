void updateUsonProtoAmbientAudio(const ScenarioSnapshot& snapshot, uint32_t now_ms) {
  auto chooseAmbientDelayMs = [now_ms](uint32_t salt) -> uint32_t {
    uint32_t min_delay = g_scene_ambient_audio_fx.delay_min_ms;
    uint32_t max_delay = g_scene_ambient_audio_fx.delay_max_ms;
    if (max_delay < min_delay) {
      const uint32_t swap = min_delay;
      min_delay = max_delay;
      max_delay = swap;
    }
    if (min_delay < 1000U) {
      min_delay = 1000U;
    }
    if (max_delay < min_delay) {
      max_delay = min_delay;
    }
    if (max_delay == min_delay) {
      return min_delay;
    }
    const uint32_t noise = hashSceneFxSeed(now_ms ^ salt ^ g_uson_ambient_next_play_ms ^ 0x4A5C93UL);
    return min_delay + (noise % (max_delay - min_delay + 1U));
  };
  auto chooseAmbientVolume = [now_ms](uint32_t salt) -> uint8_t {
    uint8_t min_vol = g_scene_ambient_audio_fx.volume_min;
    uint8_t max_vol = g_scene_ambient_audio_fx.volume_max;
    if (min_vol > FREENOVE_AUDIO_MAX_VOLUME) {
      min_vol = FREENOVE_AUDIO_MAX_VOLUME;
    }
    if (max_vol > FREENOVE_AUDIO_MAX_VOLUME) {
      max_vol = FREENOVE_AUDIO_MAX_VOLUME;
    }
    if (max_vol < min_vol) {
      max_vol = min_vol;
    }
    if (max_vol == min_vol) {
      return min_vol;
    }
    const uint32_t noise = hashSceneFxSeed(now_ms ^ salt ^ 0x5A17B4UL);
    const uint8_t span = static_cast<uint8_t>(max_vol - min_vol);
    return static_cast<uint8_t>(min_vol + (noise % (static_cast<uint32_t>(span) + 1U)));
  };

  const bool uson_scene_active =
      (snapshot.screen_scene_id != nullptr) &&
      (std::strcmp(snapshot.screen_scene_id, "SCENE_U_SON_PROTO") == 0) &&
      g_scene_ambient_audio_fx.enabled;
  if (!uson_scene_active) {
    if (g_uson_ambient_audio_active) {
      g_audio.setVolume(g_uson_ambient_restore_volume);
      g_uson_ambient_audio_active = false;
      g_uson_ambient_next_play_ms = 0U;
    }
    return;
  }

  if (!g_uson_ambient_audio_active) {
    g_uson_ambient_audio_active = true;
    g_uson_ambient_restore_volume = g_audio.volume();
    if (!g_scene_ambient_audio_fx.non_interruptive && g_audio.isPlaying()) {
      g_audio.stop();
    }
    g_uson_ambient_next_play_ms = now_ms;
  }

  if (now_ms < g_uson_ambient_next_play_ms) {
    return;
  }
  if (g_scene_ambient_audio_fx.non_interruptive && g_audio.isPlaying()) {
    g_uson_ambient_next_play_ms = now_ms + 600U;
    return;
  }
  if (!g_scene_ambient_audio_fx.non_interruptive && g_audio.isPlaying()) {
    g_audio.stop();
  }

  const uint8_t volume = chooseAmbientVolume(0x0091A24UL);
  g_audio.setVolume(volume);
  String ambient_track_path;
  if (!chooseAmbientPlaylistTrack(g_scene_ambient_audio_fx, now_ms, &ambient_track_path)) {
    ambient_track_path =
        (g_scene_ambient_audio_fx.track[0] != '\0') ? String(g_scene_ambient_audio_fx.track) : String(kUsonAmbientTrack);
  }
  const bool ok = g_audio.play(ambient_track_path.c_str());
  if (!ok) {
    g_uson_ambient_next_play_ms = now_ms + 4000U;
    Serial.printf("[AUDIO] uson ambient missing/failed track=%s\n", ambient_track_path.c_str());
    return;
  }

  const uint32_t next_delay_ms = chooseAmbientDelayMs(0x007A3B2UL);
  g_uson_ambient_next_play_ms = now_ms + next_delay_ms;
  Serial.printf("[AUDIO] uson ambient play track=%s volume=%u non_interruptive=%u next_in_ms=%lu\n",
                ambient_track_path.c_str(),
                static_cast<unsigned int>(volume),
                g_scene_ambient_audio_fx.non_interruptive ? 1U : 0U,
                static_cast<unsigned long>(next_delay_ms));
}
