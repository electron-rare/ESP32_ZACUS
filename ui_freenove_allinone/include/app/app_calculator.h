// app_calculator.h - Simple Calculator App
#pragma once

#include "app/app_runtime_types.h"
#include <Arduino.h>

class AppCalculator {
 public:
  bool init(const AppContext& ctx);
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  void onAction(const AppAction& action);
  
  // Calculator operations
  double getResult() const { return result_; }
  void reset();
  void input(const char* expression);  // Parse "12+34" or "100*2"
  
  const char* getDisplay() const;
  
 private:
  AppContext context_;
  bool initialized_ = false;
  double result_ = 0.0;
  char display_[32] = "0";
  char history_[256] = "";  // Simple history buffer
  
  double parseSimpleExpression(const char* expr);
};

extern AppCalculator g_app_calculator;
