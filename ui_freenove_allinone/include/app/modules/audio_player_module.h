// audio_player_module.h - Audio player app module wrapping AmigaAudioPlayer.
#pragma once

#include "app/app_module.h"

class AudioPlayerModule : public IAppModule {
 public:
  const char* id() const override { return "audio_player"; }

  bool onOpen(const AppEntry& entry, const char* mode, RuntimeServices* services) override;
  void onClose(const char* reason, RuntimeServices* services) override;
  void onTick(uint32_t now_ms, RuntimeServices* services) override;
  bool onAction(const char* action, const char* payload, RuntimeServices* services) override;
  AppModuleState state() const override { return state_; }
  const char* lastError() const override { return last_error_; }

 private:
  AppModuleState state_ = AppModuleState::kIdle;
  char last_error_[48] = {0};
};
