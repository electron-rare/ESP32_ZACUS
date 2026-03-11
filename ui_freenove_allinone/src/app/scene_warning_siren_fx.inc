void updateWarningSirenFx(uint32_t now_ms) {
  SceneWarningSirenFxState& siren = g_scene_warning_siren_fx;

  if (!siren.enabled) {
    if (siren.remote_started) {
      const bool stop_ok = sendWarningSirenSync("STOP", 0U);
      ZACUS_RL_LOG_MS(1200U, "[WARN_SIREN] remote stop ok=%u\n", stop_ok ? 1U : 0U);
    }
    siren.remote_started = false;
    siren.started_ms = 0U;
    siren.next_beat_ms = 0U;
    siren.next_retry_ms = 0U;
    siren.local_fallback_logged = false;
    return;
  }

  if (!siren.local_backend_available && !siren.local_fallback_logged) {
    Serial.println("[WARN_SIREN] local backend unavailable, syncing siren to A252 only");
    siren.local_fallback_logged = true;
  }

  if (!siren.remote_started) {
    if (!warningSirenRemoteSyncReady()) {
      siren.next_retry_ms = now_ms + 2000U;
      ZACUS_RL_LOG_MS(2000U, "[WARN_SIREN] remote sync waiting peer\n");
      return;
    }
    if (siren.next_retry_ms != 0U && static_cast<int32_t>(now_ms - siren.next_retry_ms) < 0) {
      return;
    }
    const bool start_ok = sendWarningSirenSync("START", 220U);
    if (!start_ok) {
      siren.next_retry_ms = now_ms + 2000U;
      ZACUS_RL_LOG_MS(1000U, "[WARN_SIREN] remote start failed, retrying\n");
      return;
    }
    siren.remote_started = true;
    siren.started_ms = now_ms;
    siren.next_retry_ms = 0U;
    siren.next_beat_ms = now_ms + kWarningSirenBeatIntervalMs;
    Serial.println("[WARN_SIREN] remote start");
  }

  if (siren.next_beat_ms != 0U && static_cast<int32_t>(now_ms - siren.next_beat_ms) < 0) {
    return;
  }

  if (!warningSirenRemoteSyncReady()) {
    siren.remote_started = false;
    siren.next_beat_ms = 0U;
    siren.next_retry_ms = now_ms + 2000U;
    ZACUS_RL_LOG_MS(2000U, "[WARN_SIREN] remote link lost, rearm\n");
    return;
  }

  const uint32_t period_ms = 1400U;
  const uint32_t half_period_ms = period_ms / 2U;
  const uint32_t elapsed_ms = now_ms - siren.started_ms;
  const uint32_t phase_ms = elapsed_ms % period_ms;
  const uint32_t ramp = (phase_ms <= half_period_ms) ? phase_ms : (period_ms - phase_ms);
  int strength = 160 + static_cast<int>((95U * ramp) / (half_period_ms == 0U ? 1U : half_period_ms));
  if (strength > 255) {
    strength = 255;
  }
  const bool beat_ok = sendWarningSirenSync("BEAT", static_cast<uint8_t>(strength));
  if (!beat_ok) {
    ZACUS_RL_LOG_MS(1000U, "[WARN_SIREN] remote beat send failed\n");
  }
  siren.next_beat_ms = now_ms + kWarningSirenBeatIntervalMs;
}
