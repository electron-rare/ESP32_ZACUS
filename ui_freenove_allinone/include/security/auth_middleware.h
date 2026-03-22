#pragma once
#include <Arduino.h>

namespace zacus {
namespace security {

// Initialize auth system - generates random token on first boot, stores in NVS
void authInit();

// Get the current auth token (for display during setup/provisioning)
const String& getAuthToken();

// Validate a Bearer token from HTTP Authorization header
bool validateAuthHeader(const String& authHeader);

// Check if auth is required (can be disabled for development)
bool isAuthRequired();

// Rate limiter - returns true if request is allowed
bool rateLimitCheck(const String& clientIP, uint32_t minIntervalMs = 100);

// Input validation - check string length and sanitize
bool validateInput(const String& input, size_t maxLength = 64);
String sanitizeInput(const String& input, size_t maxLength = 64);

} // namespace security
} // namespace zacus
