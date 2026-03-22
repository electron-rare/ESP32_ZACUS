#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace zacus {
namespace security {

// Validate JSON body with size limit
bool validateJsonBody(const String& body, size_t maxSize = 2048);

// Validate action string
bool isValidAction(const String& action);

// Validate step ID format
bool isValidStepId(const String& stepId);

} // namespace security
} // namespace zacus
