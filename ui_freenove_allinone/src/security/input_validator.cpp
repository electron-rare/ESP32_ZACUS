#include "security/input_validator.h"

#include <Arduino.h>
#include <ArduinoJson.h>

namespace zacus {
namespace security {

namespace {

constexpr const char* kLogTag = "[VALIDATE]";

// Allowed action strings (whitelist approach)
const char* kValidActions[] = {
  "play", "pause", "stop", "next", "prev", "reset",
  "volume_up", "volume_down", "mute", "unmute",
  "start_scenario", "stop_scenario",
  "led_on", "led_off", "led_color",
  "camera_start", "camera_stop",
  "status", "ping", "reboot",
  nullptr  // sentinel
};

}  // anonymous namespace

// ============================================================================
// validateJsonBody - Parse JSON with size limit, reject malformed
// ============================================================================
bool validateJsonBody(const String& body, size_t maxSize) {
  if (body.length() == 0) {
    Serial.printf("%s empty body\n", kLogTag);
    return false;
  }

  if (body.length() > maxSize) {
    Serial.printf("%s body too large: %u > %u\n", kLogTag,
                  (unsigned)body.length(), (unsigned)maxSize);
    return false;
  }

  // Try to parse as JSON to validate structure
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.printf("%s invalid JSON: %s\n", kLogTag, err.c_str());
    return false;
  }

  return true;
}

// ============================================================================
// isValidAction - Whitelist check for action strings
// ============================================================================
bool isValidAction(const String& action) {
  if (action.length() == 0 || action.length() > 32) {
    return false;
  }

  for (int i = 0; kValidActions[i] != nullptr; ++i) {
    if (action.equals(kValidActions[i])) {
      return true;
    }
  }

  Serial.printf("%s unknown action: %s\n", kLogTag, action.c_str());
  return false;
}

// ============================================================================
// isValidStepId - Validate step ID format
// Accepts: alphanumeric, underscores, hyphens, dots. Max 64 chars.
// Examples: "step_01", "intro.scene-1", "finale_2b"
// ============================================================================
bool isValidStepId(const String& stepId) {
  if (stepId.length() == 0 || stepId.length() > 64) {
    return false;
  }

  for (size_t i = 0; i < stepId.length(); ++i) {
    char c = stepId[i];
    bool valid = (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '_' || c == '-' || c == '.';
    if (!valid) {
      Serial.printf("%s invalid stepId char at pos %u: 0x%02X\n",
                    kLogTag, (unsigned)i, (unsigned char)c);
      return false;
    }
  }

  return true;
}

} // namespace security
} // namespace zacus
