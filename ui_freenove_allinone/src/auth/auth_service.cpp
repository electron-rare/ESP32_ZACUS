#include "auth/auth_service.h"

#include <Arduino.h>
#include <nvs.h>
#include <cstring>
#include <cstdio>
#include <esp_random.h>

namespace AuthService {

namespace {

constexpr const char* kNvsNamespace = "auth_service";
constexpr const char* kNvsKeyToken = "bearer_token";
constexpr const char* kLogTag = "[AUTH]";

// Global state
char g_current_token[kTokenBufferSize] = {0};
bool g_initialized = false;
bool g_auth_enabled = true;

// ============================================================================
// Generate random token (UUID4-like 32 hex chars)
// Uses esp_random() for hardware PRNG
// ============================================================================
void generateRandomToken(char* out_buffer, size_t buffer_size) {
  if (!out_buffer || buffer_size < kTokenBufferSize) {
    return;
  }

  // Generate 32 hex characters from random values
  // Each hex char represents 4 bits
  const char hex_chars[] = "0123456789abcdef";
  
  for (size_t i = 0; i < kTokenLength; i++) {
    uint32_t rand_val = esp_random();
    uint8_t nibble = (rand_val >> (i % 8)) & 0x0F;
    out_buffer[i] = hex_chars[nibble];
  }
  out_buffer[kTokenLength] = '\0';
}

// ============================================================================
// Load token from NVS Flash (namespace: "auth_service", key: "bearer_token")
// Returns true if found and loaded, false if not found or error
// ============================================================================
bool loadTokenFromNvs(char* out_buffer, size_t buffer_size) {
  if (!out_buffer || buffer_size < kTokenBufferSize) {
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    Serial.printf("%s NVS open failed (READONLY): %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  // Query size first
  size_t required_size = 0;
  err = nvs_get_str(handle, kNvsKeyToken, nullptr, &required_size);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      Serial.printf("%s NVS key not found (first boot?)\n", kLogTag);
    } else {
      Serial.printf("%s NVS size query failed: %s\n", kLogTag, esp_err_to_name(err));
    }
    nvs_close(handle);
    return false;
  }

  // Sanity check
  if (required_size > buffer_size) {
    Serial.printf("%s token buffer too small: %zu > %zu\n", kLogTag, required_size, buffer_size);
    nvs_close(handle);
    return false;
  }

  // Read the actual token
  err = nvs_get_str(handle, kNvsKeyToken, out_buffer, &buffer_size);
  nvs_close(handle);

  if (err != ESP_OK) {
    Serial.printf("%s NVS read failed: %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  return true;
}

// ============================================================================
// Save token to NVS Flash
// Creates namespace if doesn't exist, overwrites token if exists
// ============================================================================
bool saveTokenToNvs(const char* token) {
  if (!token || token[0] == '\0') {
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.printf("%s NVS open failed (READWRITE): %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  // Write token
  err = nvs_set_str(handle, kNvsKeyToken, token);
  if (err != ESP_OK) {
    Serial.printf("%s NVS write failed: %s\n", kLogTag, esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }

  // Commit (flush to flash)
  err = nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    Serial.printf("%s NVS commit failed: %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  return true;
}

// ============================================================================
// Delete token from NVS
// ============================================================================
bool deleteTokenFromNvs() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return false;
  }

  err = nvs_erase_key(handle, kNvsKeyToken);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  nvs_close(handle);

  return (err == ESP_OK);
}

}  // namespace

// ============================================================================
// PUBLIC API IMPLEMENTATIONS
// ============================================================================

bool init() {
  if (g_initialized) {
    return true;
  }

  // Try to load token from NVS (persistent storage)
  if (!loadTokenFromNvs(g_current_token, sizeof(g_current_token))) {
    // Not found or error → generate new random token
    Serial.printf("%s generating new token\n", kLogTag);
    generateRandomToken(g_current_token, sizeof(g_current_token));

    // Try to save (optional, can fail on first boot if NVS not ready yet)
    if (!saveTokenToNvs(g_current_token)) {
      Serial.printf("%s warning: could not persist token to NVS, will regenerate on next boot\n", kLogTag);
    }
  }

  g_initialized = true;
  Serial.printf("%s initialized token=%s\n", kLogTag, g_current_token);
  Serial.printf("%s use header: Authorization: Bearer %s\n", kLogTag, g_current_token);
  return true;
}

AuthStatus validateBearerToken(const char* auth_header) {
  // Check if service initialized
  if (!g_initialized) {
    return AuthStatus::kUninitialized;
  }

  // Check if auth globally disabled (testing only)
  if (!g_auth_enabled) {
    return AuthStatus::kOk;
  }

  // Check if header provided
  if (!auth_header || auth_header[0] == '\0') {
    return AuthStatus::kMissingHeader;
  }

  // Check "Bearer " prefix (case-sensitive)
  if (std::strncmp(auth_header, "Bearer ", 7) != 0) {
    return AuthStatus::kInvalidFormat;
  }

  // Extract token part
  const char* provided_token = &auth_header[7];
  if (!provided_token || provided_token[0] == '\0') {
    return AuthStatus::kInvalidFormat;
  }

  // Copy to buffer and trim whitespace
  char token_copy[kTokenBufferSize];
  std::strncpy(token_copy, provided_token, sizeof(token_copy) - 1);
  token_copy[sizeof(token_copy) - 1] = '\0';

  // Trim trailing whitespace (newlines, spaces, etc)
  size_t len = std::strlen(token_copy);
  while (len > 0 && (token_copy[len - 1] == ' ' ||
                     token_copy[len - 1] == '\n' ||
                     token_copy[len - 1] == '\r' ||
                     token_copy[len - 1] == '\t')) {
    token_copy[len - 1] = '\0';
    len--;
  }

  // Compare with stored token (case-sensitive, exact match)
  if (std::strcmp(token_copy, g_current_token) != 0) {
    return AuthStatus::kInvalidToken;
  }

  // Token is valid!
  return AuthStatus::kOk;
}

bool getCurrentToken(char* out_token_buffer, size_t buffer_size) {
  if (!out_token_buffer || buffer_size < kTokenBufferSize || !g_initialized) {
    return false;
  }
  std::strncpy(out_token_buffer, g_current_token, buffer_size - 1);
  out_token_buffer[buffer_size - 1] = '\0';
  return true;
}

bool rotateToken(char* out_new_token_buffer, size_t buffer_size) {
  if (!out_new_token_buffer || buffer_size < kTokenBufferSize) {
    return false;
  }

  // Generate new random token
  char new_token[kTokenBufferSize];
  generateRandomToken(new_token, sizeof(new_token));

  // Persist to NVS
  if (!saveTokenToNvs(new_token)) {
    Serial.printf("%s rotation failed: NVS write error\n", kLogTag);
    return false;
  }

  // Update global state
  std::strncpy(g_current_token, new_token, sizeof(g_current_token) - 1);
  g_current_token[sizeof(g_current_token) - 1] = '\0';

  // Return new token to caller
  std::strncpy(out_new_token_buffer, new_token, buffer_size - 1);
  out_new_token_buffer[buffer_size - 1] = '\0';

  Serial.printf("%s rotated - new token=%s\n", kLogTag, new_token);
  return true;
}

bool reset() {
  // Delete from NVS
  if (!deleteTokenFromNvs()) {
    Serial.printf("%s reset: NVS delete failed\n", kLogTag);
    return false;
  }

  // Generate new token
  char new_token[kTokenBufferSize];
  generateRandomToken(new_token, sizeof(new_token));

  // Persist
  if (!saveTokenToNvs(new_token)) {
    Serial.printf("%s reset: NVS save failed\n", kLogTag);
    return false;
  }

  // Update global
  std::strncpy(g_current_token, new_token, sizeof(g_current_token) - 1);
  g_current_token[sizeof(g_current_token) - 1] = '\0';

  Serial.printf("%s reset - new token=%s\n", kLogTag, new_token);
  return true;
}

const char* statusMessage(AuthStatus status) {
  switch (status) {
    case AuthStatus::kOk:
      return "OK";
    case AuthStatus::kMissingHeader:
      return "Missing Authorization header";
    case AuthStatus::kInvalidFormat:
      return "Invalid Bearer format (expected: Authorization: Bearer <token>)";
    case AuthStatus::kInvalidToken:
      return "Invalid token";
    case AuthStatus::kUninitialized:
      return "Auth service not initialized";
    case AuthStatus::kInternalError:
      return "Internal error (NVS)";
    default:
      return "Unknown error";
  }
}

bool isEnabled() {
  return g_auth_enabled;
}

void setEnabled(bool enabled) {
  g_auth_enabled = enabled;
  Serial.printf("%s auth %s\n", kLogTag, enabled ? "ENABLED" : "DISABLED (testing only)");
}

}  // namespace AuthService
