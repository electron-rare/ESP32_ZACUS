// app_timer.h - Timer/Chrono App
#pragma once

#include "app/app_runtime_types.h"
#include <Arduino.h>

class AppTimer {
 public:
  bool init(const AppContext& ctx);
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  void onAction(const AppAction& action);
  
  // Timer control
  void startCountdown(uint32_t seconds);
  void pause();
  void resume();
  void reset();
  
  // Status
  uint32_t getRemainingSeconds() const { return remaining_ms_ / 1000; }
  bool isRunning() const { return running_; }
  
  const char* getDisplay() const;
  
 private:
  AppContext context_;
  bool initialized_ = false;
  bool running_ = false;
  uint32_t remaining_ms_ = 0;
  uint32_t total_ms_ = 0;
  char display_[32] = "00:00";
  
  void updateDisplay();
  void onTimeout();
};

extern AppTimer g_app_timer;
