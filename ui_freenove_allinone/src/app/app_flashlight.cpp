// app_flashlight.cpp - Flashlight Implementation
#include "app/app_flashlight.h"

AppFlashlight g_app_flashlight;

bool AppFlashlight::init(const AppContext& ctx) {
  context_ = ctx;
  initialized_ = true;
  
  // Initialize LED GPIO
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);
  
  Serial.println("[APP_LED] Initialized GPIO 13");
  return true;
}

void AppFlashlight::onStart() {
  turnOff();
  Serial.println("[APP_LED] Started");
}

void AppFlashlight::onStop() {
  turnOff();
  Serial.println("[APP_LED] Stopped");
}

void AppFlashlight::onTick(uint32_t dt_ms) {
  // No continuous processing needed
  (void)dt_ms;
}

void AppFlashlight::onAction(const AppAction& action) {
  if (!initialized_) return;
  
  Serial.printf("[APP_LED] Action: %s payload=%s\n", action.name, action.payload);
  
  if (strcmp(action.name, "on") == 0) {
    turnOn();
  } else if (strcmp(action.name, "off") == 0) {
    turnOff();
  } else if (strcmp(action.name, "toggle") == 0) {
    toggle();
  } else if (strcmp(action.name, "intensity") == 0) {
    int percent = atoi(action.payload);
    setIntensity(constrain(percent, 0, 100));
  }
}

void AppFlashlight::turnOn() {
  is_on_ = true;
  updateLED();
  Serial.printf("[APP_LED] ON (intensity=%u%%)\n", (intensity_ * 100) / 255);
}

void AppFlashlight::turnOff() {
  is_on_ = false;
  updateLED();
  Serial.println("[APP_LED] OFF");
}

void AppFlashlight::setIntensity(uint8_t percent) {
  intensity_ = map(constrain(percent, 0, 100), 0, 100, 0, 255);
  if (is_on_) {
    updateLED();
  }
  Serial.printf("[APP_LED] Intensity: %u%%\n", percent);
}

void AppFlashlight::toggle() {
  if (is_on_) {
    turnOff();
  } else {
    turnOn();
  }
}

const char* AppFlashlight::getStatusString() const {
  static char status[32];
  snprintf(status, sizeof(status), "state=%s intensity=%u%%",
           is_on_ ? "ON" : "OFF", (intensity_ * 100) / 255);
  return status;
}

void AppFlashlight::updateLED() {
  if (is_on_) {
    // Use PWM to control brightness
    analogWrite(LED_GPIO, intensity_);
  } else {
    digitalWrite(LED_GPIO, LOW);
  }
}
