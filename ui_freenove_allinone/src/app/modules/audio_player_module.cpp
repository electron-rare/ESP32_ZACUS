// audio_player_module.cpp - Audio player module bridging to AmigaAudioPlayer.
#include "app/modules/audio_player_module.h"

#include <cstring>

#include "runtime/runtime_services.h"

bool AudioPlayerModule::onOpen(const AppEntry& entry, const char* mode, RuntimeServices* services) {
  (void)entry;
  (void)mode;
  if (services == nullptr || services->audio == nullptr) {
    std::strncpy(last_error_, "no_audio_service", sizeof(last_error_) - 1U);
    state_ = AppModuleState::kError;
    return false;
  }
  state_ = AppModuleState::kRunning;
  last_error_[0] = '\0';
  Serial.println("[APP_MOD] audio_player open");
  return true;
}

void AudioPlayerModule::onClose(const char* reason, RuntimeServices* services) {
  (void)reason;
  if (services != nullptr && services->audio != nullptr) {
    services->audio->stop();
  }
  state_ = AppModuleState::kIdle;
  Serial.printf("[APP_MOD] audio_player close reason=%s\n", reason != nullptr ? reason : "n/a");
}

void AudioPlayerModule::onTick(uint32_t now_ms, RuntimeServices* services) {
  (void)now_ms;
  (void)services;
  // AmigaAudioPlayer is ticked separately from main.cpp for now.
}

bool AudioPlayerModule::onAction(const char* action, const char* payload, RuntimeServices* services) {
  if (action == nullptr || services == nullptr || services->audio == nullptr) {
    return false;
  }

  if (std::strcmp(action, "play") == 0) {
    return services->audio->play(payload);
  }
  if (std::strcmp(action, "stop") == 0) {
    services->audio->stop();
    return true;
  }
  if (std::strcmp(action, "pause") == 0) {
    services->audio->stop();
    return true;
  }
  if (std::strcmp(action, "set_volume") == 0 && payload != nullptr) {
    const int vol = atoi(payload);
    if (vol >= 0 && vol <= 100) {
      // Map 0-100 to 0-21.
      const uint8_t mapped = static_cast<uint8_t>((vol * 21U) / 100U);
      services->audio->setVolume(mapped);
      return true;
    }
  }

  Serial.printf("[APP_MOD] audio_player unknown action=%s\n", action);
  return false;
}
