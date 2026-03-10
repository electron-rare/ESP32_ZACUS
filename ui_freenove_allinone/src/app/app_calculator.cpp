// app_calculator.cpp - Calculator Implementation
#include "app/app_calculator.h"
#include "core/str_utils.h"

AppCalculator g_app_calculator;

bool AppCalculator::init(const AppContext& ctx) {
  context_ = ctx;
  initialized_ = true;
  reset();
  Serial.println("[APP_CALC] Initialized");
  return true;
}

void AppCalculator::onStart() {
  reset();
  Serial.println("[APP_CALC] Started");
}

void AppCalculator::onStop() {
  Serial.println("[APP_CALC] Stopped");
}

void AppCalculator::onTick(uint32_t dt_ms) {
  // No continuous processing needed
  (void)dt_ms;
}

void AppCalculator::onAction(const AppAction& action) {
  if (!initialized_) return;
  
  Serial.printf("[APP_CALC] Action: %s payload=%s\n", action.name, action.payload);
  
  if (strcmp(action.name, "input") == 0) {
    input(action.payload);
  } else if (strcmp(action.name, "reset") == 0) {
    reset();
  }
}

void AppCalculator::reset() {
  result_ = 0.0;
  core::copyText(display_, sizeof(display_), "0");
  history_[0] = '\0';
}

void AppCalculator::input(const char* expression) {
  if (!expression || strlen(expression) == 0) return;
  
  result_ = parseSimpleExpression(expression);
  snprintf(display_, sizeof(display_), "%.2f", result_);
  
  // Add to history
  if (strlen(history_) + strlen(expression) + 10 < sizeof(history_)) {
    strcat(history_, expression);
    strcat(history_, " = ");
    strcat(history_, display_);
    strcat(history_, "\n");
  }
  
  Serial.printf("[APP_CALC] %s = %.2f\n", expression, result_);
}

const char* AppCalculator::getDisplay() const {
  return display_;
}

double AppCalculator::parseSimpleExpression(const char* expr) {
  // Simple parser for basic operations: "12+34", "100*2", "50-20", "100/4"
  if (!expr || strlen(expr) == 0) return 0.0;
  
  char op = ' ';
  double a = 0.0, b = 0.0;
  int n = sscanf(expr, "%lf%c%lf", &a, &op, &b);
  
  if (n < 3) {
    // Try parsing just a number
    return atof(expr);
  }
  
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return (b != 0.0) ? a / b : 0.0;
    default: return 0.0;
  }
}
