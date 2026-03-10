// app_audio.h - Audio Player App
// Features: Play/Pause/Stop, Volume control, Playlist navigation
#pragma once

#include "app/app_runtime_types.h"
#include <Arduino.h>

class AppAudio {
 public:
  // Lifecycle
  bool init(const AppContext& ctx);
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  void onAction(const AppAction& action);
  
  // Audio control
  bool play(const char* filepath);
  void pause();
  void resume();
  void stop();
  void setVolume(uint8_t vol_percent);  // 0-100
  
  // Status
  const char* getStatus() const;
  uint32_t getCurrentPlaybackMs() const;
  uint32_t getTotalDurationMs() const;
  
 private:
  AppContext context_;
  bool initialized_ = false;
  
  enum {
    STATE_IDLE = 0,
    STATE_PLAYING = 1,
    STATE_PAUSED = 2,
    STATE_ERROR = 3,
  };
  
  uint8_t current_state_ = STATE_IDLE;
  char current_file_[96] = "";
  uint8_t volume_percent_ = 50;
  uint32_t playback_ms_ = 0;
  uint32_t duration_ms_ = 0;
  uint32_t last_tick_ms_ = 0;
};

// Global instance
extern AppAudio g_app_audio;
