// app_timer.cpp - Timer Implementation
#include "app/app_timer.h"
#include "hardware_manager.h"

AppTimer g_app_timer;

bool AppTimer::init(const AppContext& ctx) {
  context_ = ctx;
  initialized_ = true;
  reset();
  Serial.println("[APP_TIMER] Initialized");
  return true;
}

void AppTimer::onStart() {
  reset();
  Serial.println("[APP_TIMER] Started");
}

void AppTimer::onStop() {
  if (running_) {
    running_ = false;
  }
  Serial.println("[APP_TIMER] Stopped");
}

void AppTimer::onTick(uint32_t dt_ms) {
  if (!initialized_ || !running_) return;
  
  if (remaining_ms_ > dt_ms) {
    remaining_ms_ -= dt_ms;
  } else {
    remaining_ms_ = 0;
    running_ = false;
    onTimeout();
  }
  
  updateDisplay();
}

void AppTimer::onAction(const AppAction& action) {
  if (!initialized_) return;
  
  Serial.printf("[APP_TIMER] Action: %s payload=%s\n", action.name, action.payload);
  
  if (strcmp(action.name, "start") == 0) {
    uint32_t seconds = atoi(action.payload);
    startCountdown(seconds);
  } else if (strcmp(action.name, "pause") == 0) {
    pause();
  } else if (strcmp(action.name, "resume") == 0) {
    resume();
  } else if (strcmp(action.name, "reset") == 0) {
    reset();
  }
}

void AppTimer::startCountdown(uint32_t seconds) {
  total_ms_ = seconds * 1000;
  remaining_ms_ = total_ms_;
  running_ = true;
  updateDisplay();
  Serial.printf("[APP_TIMER] Starting countdown: %u seconds\n", seconds);
}

void AppTimer::pause() {
  if (running_) {
    running_ = false;
    Serial.printf("[APP_TIMER] Paused at %u seconds\n", remaining_ms_ / 1000);
  }
}

void AppTimer::resume() {
  if (!running_ && remaining_ms_ > 0) {
    running_ = true;
    Serial.printf("[APP_TIMER] Resumed\n");
  }
}

void AppTimer::reset() {
  running_ = false;
  remaining_ms_ = 0;
  total_ms_ = 0;
  strncpy(display_, "00:00", sizeof(display_) - 1);
}

void AppTimer::updateDisplay() {
  uint32_t minutes = remaining_ms_ / 60000;
  uint32_t seconds = (remaining_ms_ % 60000) / 1000;
  snprintf(display_, sizeof(display_), "%02u:%02u", minutes, seconds);
}

void AppTimer::onTimeout() {
  Serial.println("[APP_TIMER] TIMEOUT - Playing alarm");
  
  // Play beep alarm if buzzer available
  if (context_.hardware) {
    for (int i = 0; i < 5; i++) {
      // TODO: Use buzzer/audio to play alarm sound
      // context_.hardware->buzzer(1000, 100);  // Frequency, duration
      delay(100);
    }
  }
  
  updateDisplay();
}
