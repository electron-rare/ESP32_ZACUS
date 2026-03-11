void updateSceneBacklightFx(uint32_t now_ms) {
  if (!g_scene_backlight_fx.enabled) {
    if (g_lcd_backlight_level != g_lcd_backlight_base_level) {
      applyLcdBacklightEffective(g_lcd_backlight_base_level);
    }
    return;
  }

  SceneBacklightFxState& fx = g_scene_backlight_fx;
  if (fx.min_level > fx.max_level) {
    const uint8_t swap = fx.min_level;
    fx.min_level = fx.max_level;
    fx.max_level = swap;
  }
  if (fx.max_level > 125U) {
    fx.max_level = 125U;
  }
  if (fx.base_level < fx.min_level) {
    fx.base_level = fx.min_level;
  } else if (fx.base_level > fx.max_level) {
    fx.base_level = fx.max_level;
  }

  if (fx.level_sync) {
    const HardwareManager::Snapshot& hw = g_hardware.snapshotRef();
    uint8_t level_pct = hw.mic_level_percent;
    if (level_pct > 100U) {
      level_pct = 100U;
    }
    int level = static_cast<int>(fx.base_level);
    if (fx.max_level > fx.base_level) {
      const uint16_t span = static_cast<uint16_t>(fx.max_level - fx.base_level);
      level = static_cast<int>(fx.base_level + static_cast<uint8_t>((static_cast<uint16_t>(span) * level_pct) / 100U));
    }
    if (level_pct < 5U) {
      level = fx.min_level;
    }
    if (fx.spike_pct > 0U && level_pct > 12U) {
      const uint32_t noise = hashSceneFxSeed(now_ms ^ (static_cast<uint32_t>(level_pct) << 12U) ^ 0x42A7UL);
      const uint8_t spike_chance =
          static_cast<uint8_t>((static_cast<uint16_t>(fx.spike_pct) * static_cast<uint16_t>(level_pct)) / 100U);
      if ((noise % 100U) < spike_chance) {
        const uint8_t boost = static_cast<uint8_t>(2U + ((noise >> 8U) % 14U));
        level += boost;
      }
    }
    if (level < fx.min_level) {
      level = fx.min_level;
    } else if (level > fx.max_level) {
      level = fx.max_level;
    }
    const uint8_t target = static_cast<uint8_t>(level);
    if (target != g_lcd_backlight_level) {
      applyLcdBacklightEffective(target);
    }
    return;
  }

  if (fx.next_spike_ms == 0U || static_cast<int32_t>(now_ms - fx.next_spike_ms) >= 0) {
    const uint32_t noise = hashSceneFxSeed(now_ms ^ (static_cast<uint32_t>(fx.glitch_pct) << 8U) ^
                                           (static_cast<uint32_t>(fx.spike_pct) << 16U));
    uint8_t spike_chance = static_cast<uint8_t>(
        (static_cast<uint16_t>(fx.spike_pct) * static_cast<uint16_t>(50U + fx.glitch_pct)) / 100U);
    if (spike_chance > 95U) {
      spike_chance = 95U;
    }
    if ((noise % 100U) < spike_chance) {
      fx.spike_end_ms = now_ms + 12U + ((noise >> 8U) % 38U);
      const uint8_t span = static_cast<uint8_t>(fx.max_level - fx.min_level);
      const uint8_t spike_add = static_cast<uint8_t>(4U + ((noise >> 16U) % (span > 0U ? span : 1U)));
      uint16_t level = static_cast<uint16_t>(fx.base_level) + spike_add;
      if (level > fx.max_level) {
        level = fx.max_level;
      }
      fx.spike_level = static_cast<uint8_t>(level);
    } else {
      fx.spike_end_ms = 0U;
      fx.spike_level = fx.base_level;
    }
    fx.next_spike_ms = now_ms + 180U + ((noise >> 21U) % 1300U);
  }

  int level = static_cast<int>(fx.base_level);
  const int jitter_span = (fx.glitch_pct < 20U) ? 0 : static_cast<int>(fx.glitch_pct / 25U);
  if (jitter_span > 0) {
    const uint32_t noise = hashSceneFxSeed(now_ms ^ 0x45A2B31UL);
    const uint32_t range = static_cast<uint32_t>(jitter_span * 2 + 1);
    level += static_cast<int>(noise % range) - jitter_span;
  }
  if (fx.spike_end_ms != 0U && static_cast<int32_t>(now_ms - fx.spike_end_ms) < 0) {
    level = fx.spike_level;
  }

  if (level < fx.min_level) {
    level = fx.min_level;
  } else if (level > fx.max_level) {
    level = fx.max_level;
  }
  const uint8_t target = static_cast<uint8_t>(level);
  if (target != g_lcd_backlight_level) {
    applyLcdBacklightEffective(target);
  }
}
