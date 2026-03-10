// app_flashlight.h - Flashlight/LED App
#pragma once

#include "app/app_runtime_types.h"
#include <Arduino.h>

class AppFlashlight {
 public:
  bool init(const AppContext& ctx);
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  void onAction(const AppAction& action);
  
  // Flashlight control
  void turnOn();
  void turnOff();
  void setIntensity(uint8_t percent);  // 0-100 (PWM duty cycle)
  void toggle();
  
  // Status
  bool isOn() const { return is_on_; }
  uint8_t getIntensity() const { return intensity_; }
  const char* getStatusString() const;
  
 private:
  AppContext context_;
  bool initialized_ = false;
  bool is_on_ = false;
  uint8_t intensity_ = 255;  // 0-255 for PWM, mapped to 0-100% UI
  
  static constexpr uint8_t LED_GPIO = 13;  // From RC_FINAL_BOARD.md
  
  void updateLED();
};

extern AppFlashlight g_app_flashlight;
