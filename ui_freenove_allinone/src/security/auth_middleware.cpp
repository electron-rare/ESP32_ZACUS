#include "security/auth_middleware.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <map>
#include <cstring>

namespace zacus {
namespace security {

namespace {

constexpr const char* kNvsNamespace = "zacus_auth";
constexpr const char* kNvsKeyToken  = "token";
constexpr const char* kLogTag       = "[SEC]";
constexpr size_t kTokenHexLen       = 32;  // 32 hex chars = 128 bits

String g_auth_token;
bool   g_initialized = false;

// Rate limiter state: IP -> last request timestamp (ms)
std::map<String, uint32_t> g_rate_map;

// Maximum entries in rate map to prevent memory exhaustion
constexpr size_t kMaxRateEntries = 32;

// ============================================================================
// Generate a 32-char random hex token using hardware RNG
// ============================================================================
String generateToken() {
  static const char hex[] = "0123456789abcdef";
  char buf[kTokenHexLen + 1];

  for (size_t i = 0; i < kTokenHexLen; i += 8) {
    uint32_t r = esp_random();
    for (int j = 0; j < 8 && (i + j) < kTokenHexLen; ++j) {
      buf[i + j] = hex[(r >> (j * 4)) & 0x0F];
    }
  }
  buf[kTokenHexLen] = '\0';
  return String(buf);
}

// ============================================================================
// Constant-time string comparison to prevent timing attacks
// ============================================================================
bool constantTimeEquals(const String& a, const String& b) {
  if (a.length() != b.length()) {
    // Still do work proportional to length to avoid leaking length info
    volatile uint8_t dummy = 0;
    size_t maxLen = max(a.length(), b.length());
    for (size_t i = 0; i < maxLen; ++i) {
      dummy |= 0xFF;
    }
    (void)dummy;
    return false;
  }

  volatile uint8_t result = 0;
  for (size_t i = 0; i < a.length(); ++i) {
    result |= (uint8_t)(a[i] ^ b[i]);
  }
  return result == 0;
}

// ============================================================================
// Evict oldest entries from rate map when it gets too large
// ============================================================================
void pruneRateMap() {
  if (g_rate_map.size() <= kMaxRateEntries) return;

  // Find and remove the oldest entry
  auto oldest = g_rate_map.begin();
  for (auto it = g_rate_map.begin(); it != g_rate_map.end(); ++it) {
    if (it->second < oldest->second) {
      oldest = it;
    }
  }
  if (oldest != g_rate_map.end()) {
    g_rate_map.erase(oldest);
  }
}

}  // anonymous namespace

// ============================================================================
// authInit - Load or generate token, persist to NVS
// ============================================================================
void authInit() {
  if (g_initialized) return;

  Preferences prefs;
  prefs.begin(kNvsNamespace, false);  // read-write

  String stored = prefs.getString(kNvsKeyToken, "");

  if (stored.length() == kTokenHexLen) {
    g_auth_token = stored;
    Serial.printf("%s loaded token from NVS\n", kLogTag);
  } else {
    g_auth_token = generateToken();
    prefs.putString(kNvsKeyToken, g_auth_token);
    Serial.printf("%s generated new token, saved to NVS\n", kLogTag);
  }

  prefs.end();
  g_initialized = true;

  Serial.printf("%s token=%s\n", kLogTag, g_auth_token.c_str());
  Serial.printf("%s Authorization: Bearer %s\n", kLogTag, g_auth_token.c_str());
}

// ============================================================================
// getAuthToken - Return current token reference
// ============================================================================
const String& getAuthToken() {
  return g_auth_token;
}

// ============================================================================
// validateAuthHeader - Parse "Bearer <token>" and compare
// ============================================================================
bool validateAuthHeader(const String& authHeader) {
  if (!g_initialized) return false;

  // Must start with "Bearer " (7 chars)
  if (!authHeader.startsWith("Bearer ")) {
    return false;
  }

  String provided = authHeader.substring(7);
  provided.trim();

  if (provided.length() == 0) {
    return false;
  }

  return constantTimeEquals(provided, g_auth_token);
}

// ============================================================================
// isAuthRequired - Check ZACUS_AUTH_ENABLED compile flag
// ============================================================================
bool isAuthRequired() {
#if defined(ZACUS_AUTH_ENABLED) && ZACUS_AUTH_ENABLED
  return true;
#else
  return false;
#endif
}

// ============================================================================
// rateLimitCheck - Token bucket per IP
// ============================================================================
bool rateLimitCheck(const String& clientIP, uint32_t minIntervalMs) {
  uint32_t now = millis();

  auto it = g_rate_map.find(clientIP);
  if (it != g_rate_map.end()) {
    uint32_t elapsed = now - it->second;
    if (elapsed < minIntervalMs) {
      return false;  // Too fast, reject
    }
    it->second = now;
    return true;
  }

  // New IP entry
  pruneRateMap();
  g_rate_map[clientIP] = now;
  return true;
}

// ============================================================================
// validateInput - Length check, null byte check, control char rejection
// ============================================================================
bool validateInput(const String& input, size_t maxLength) {
  if (input.length() == 0) return false;
  if (input.length() > maxLength) return false;

  // Check for embedded null bytes
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '\0') return false;
    // Reject control characters (0x00-0x1F) except tab, newline, carriage return
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
  }

  return true;
}

// ============================================================================
// sanitizeInput - Trim, truncate, strip control chars
// ============================================================================
String sanitizeInput(const String& input, size_t maxLength) {
  String result;
  result.reserve(min(input.length(), maxLength));

  for (size_t i = 0; i < input.length() && result.length() < maxLength; ++i) {
    char c = input[i];
    // Skip null bytes and control characters (except whitespace)
    if (c == '\0') continue;
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
    result += c;
  }

  result.trim();
  return result;
}

} // namespace security
} // namespace zacus
