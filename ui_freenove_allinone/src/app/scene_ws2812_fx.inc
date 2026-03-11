void updateSceneWs2812Fx(uint32_t now_ms) {
  if (!g_scene_ws2812_fx.single_random_blink || !g_scene_ws2812_fx.level_sync || !g_hardware_started) {
    return;
  }
  if (now_ms < g_scene_ws2812_runtime_next_update_ms) {
    return;
  }
  g_scene_ws2812_runtime_next_update_ms = now_ms + 70U;

  uint8_t min_brightness = g_scene_ws2812_fx.min_brightness;
  uint8_t max_brightness = g_scene_ws2812_fx.max_brightness;
  if (max_brightness > 99U) {
    max_brightness = 99U;
  }
  if (min_brightness > max_brightness) {
    min_brightness = max_brightness;
  }

  const HardwareManager::Snapshot& hw = g_hardware.snapshotRef();
  uint8_t level_pct = hw.mic_level_percent;
  if (level_pct > 100U) {
    level_pct = 100U;
  }
  uint8_t target = min_brightness;
  if (max_brightness > min_brightness) {
    const uint16_t span = static_cast<uint16_t>(max_brightness - min_brightness);
    target = static_cast<uint8_t>(min_brightness + static_cast<uint8_t>((span * level_pct) / 100U));
  }
  if (level_pct > 55U) {
    const uint32_t noise = hashSceneFxSeed(now_ms ^ 0x9A5F13UL ^ (static_cast<uint32_t>(level_pct) << 6U));
    const uint8_t chance = static_cast<uint8_t>(6U + (g_scene_backlight_fx.glitch_pct / 8U));
    if ((noise % 100U) < chance) {
      const uint8_t boost = static_cast<uint8_t>(4U + ((noise >> 8U) % 10U));
      const uint16_t boosted = static_cast<uint16_t>(target) + boost;
      target = static_cast<uint8_t>((boosted > max_brightness) ? max_brightness : boosted);
    }
  }

  uint8_t out_r = g_scene_ws2812_fx.r;
  uint8_t out_g = g_scene_ws2812_fx.g;
  uint8_t out_b = g_scene_ws2812_fx.b;
  if (g_scene_ws2812_fx.dynamic_color) {
    const uint16_t peak_hz = (hw.mic_spectrum_peak_hz > 0U) ? hw.mic_spectrum_peak_hz : hw.mic_freq_hz;
    if (peak_hz > 0U) {
      if (peak_hz < 430U) {
        out_r = 0xFFU;
        out_g = 0x38U;
        out_b = 0x28U;
      } else if (peak_hz > 450U) {
        out_r = 0x36U;
        out_g = 0x7EU;
        out_b = 0xFFU;
      } else {
        out_r = 0x4FU;
        out_g = 0xFFU;
        out_b = 0x7CU;
      }
    } else {
      out_r = static_cast<uint8_t>(32U + ((223U * level_pct) / 100U));
      out_g = static_cast<uint8_t>(32U + ((191U * level_pct) / 100U));
      out_b = static_cast<uint8_t>(255U - ((191U * level_pct) / 100U));
    }
  }

  if (target == g_scene_ws2812_runtime_brightness &&
      out_r == g_scene_ws2812_fx.r &&
      out_g == g_scene_ws2812_fx.g &&
      out_b == g_scene_ws2812_fx.b) {
    return;
  }
  g_scene_ws2812_fx.r = out_r;
  g_scene_ws2812_fx.g = out_g;
  g_scene_ws2812_fx.b = out_b;
  g_scene_ws2812_runtime_brightness = target;
  g_hardware.setSceneSingleRandomBlink(true,
                                       out_r,
                                       out_g,
                                       out_b,
                                       g_scene_ws2812_runtime_brightness,
                                       g_scene_ws2812_fx.dominant_band_single);
}
