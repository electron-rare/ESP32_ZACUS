#include "core/wifi_config.h"
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>
#include <cctype>

namespace ZacusWiFiConfig {

// ============================================================================
// Validation Functions
// ============================================================================

bool isValidSSID(const char* ssid) {
  if (!ssid) return false;
  
  size_t len = strlen(ssid);
  
  // Empty SSID is allowed (will trigger AP mode)
  if (len == 0) return true;
  
  // Max 32 bytes per WiFi spec
  if (len > kMaxSSIDLen) return false;
  
  // Check for null terminators in middle (no embedded nulls)
  for (size_t i = 0; i < len; i++) {
    if (ssid[i] == '\0') return false;
  }
  
  return true;
}

bool isValidPassword(const char* password) {
  if (!password) return false;
  
  size_t len = strlen(password);
  
  // Empty password allowed (open network, will connect without password)
  if (len == 0) return true;
  
  // WPA2/WPA3 minimum requirement: 8 characters
  if (len > 0 && len < 8) {
    Serial.println("[WiFiConfig] Password too short (min 8 chars)");
    return false;
  }
  
  // Max 63 bytes per WiFi spec (we use 64 for buffer)
  if (len > 63) {
    Serial.println("[WiFiConfig] Password too long (max 63 chars)");
    return false;
  }
  
  // Check for control characters (security: avoid binary/null injection)
  for (size_t i = 0; i < len; i++) {
    unsigned char c = static_cast<unsigned char>(password[i]);
    if (c < 32 || c == 127) {
      Serial.println("[WiFiConfig] Password contains control characters");
      return false;
    }
  }
  
  return true;
}

// ============================================================================
// NVS Storage Functions
// ============================================================================

bool readSSIDFromNVS(char* out_ssid, size_t max_len) {
  if (!out_ssid || max_len == 0) return false;
  
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return false;  // NVS not initialized
  }
  
  size_t required_size = 0;
  err = nvs_get_str(handle, kNvsKeySSID, NULL, &required_size);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;  // Key not found
  }
  
  // Ensure we don't overflow
  if (required_size > max_len) {
    nvs_close(handle);
    return false;
  }
  
  err = nvs_get_str(handle, kNvsKeySSID, out_ssid, &max_len);
  nvs_close(handle);
  
  return (err == ESP_OK);
}

bool readPasswordFromNVS(char* out_password, size_t max_len) {
  if (!out_password || max_len == 0) return false;
  
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return false;
  }
  
  size_t required_size = 0;
  err = nvs_get_str(handle, kNvsKeyPassword, NULL, &required_size);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;
  }
  
  if (required_size > max_len) {
    nvs_close(handle);
    return false;
  }
  
  err = nvs_get_str(handle, kNvsKeyPassword, out_password, &max_len);
  nvs_close(handle);
  
  return (err == ESP_OK);
}

bool writeSSIDToNVS(const char* ssid) {
  if (!isValidSSID(ssid)) return false;
  
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return false;
  }
  
  err = nvs_set_str(handle, kNvsKeySSID, ssid);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;
  }
  
  // Set "credentials present" flag
  err = nvs_set_u8(handle, kNvsKeyIsSet, 1);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;
  }
  
  err = nvs_commit(handle);
  nvs_close(handle);
  
  return (err == ESP_OK);
}

bool writePasswordToNVS(const char* password) {
  if (!isValidPassword(password)) return false;
  
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return false;
  }
  
  err = nvs_set_str(handle, kNvsKeyPassword, password);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;
  }
  
  err = nvs_commit(handle);
  nvs_close(handle);
  
  return (err == ESP_OK);
}

bool hasCredentialsInNVS() {
  char ssid[kMaxSSIDLen + 1];
  return readSSIDFromNVS(ssid, sizeof(ssid));
}

// ============================================================================
// Factory Reset
// ============================================================================

bool clearNVSWiFiConfig() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return false;
  }
  
  nvs_erase_key(handle, kNvsKeySSID);
  nvs_erase_key(handle, kNvsKeyPassword);
  nvs_erase_key(handle, kNvsKeyIsSet);
  
  err = nvs_commit(handle);
  nvs_close(handle);
  
  return (err == ESP_OK);
}

// ============================================================================
// Serial Command Parsing
// ============================================================================

bool parseWifiConfigCommand(const char* cmd_line,
                           char* out_ssid,
                           char* out_password) {
  if (!cmd_line || !out_ssid || !out_password) return false;
  
  // Make a copy we can modify (strtok modifies input)
  char work_buffer[kSerialCmdMaxLen];
  strncpy(work_buffer, cmd_line, sizeof(work_buffer) - 1);
  work_buffer[sizeof(work_buffer) - 1] = '\0';
  
  // Skip "WIFI_CONFIG " prefix
  const char* prefix = "WIFI_CONFIG ";
  if (strncmp(work_buffer, prefix, strlen(prefix)) != 0) {
    return false;
  }
  
  char* ptr = work_buffer + strlen(prefix);
  
  // Extract SSID (everything up to first space)
  int ssid_len = 0;
  while (*ptr && *ptr != ' ' && ssid_len < (int)kMaxSSIDLen) {
    out_ssid[ssid_len++] = *ptr++;
  }
  out_ssid[ssid_len] = '\0';
  
  // Skip spaces
  while (*ptr && *ptr == ' ') {
    ptr++;
  }
  
  // Extract password (everything remaining)
  int pwd_len = 0;
  while (*ptr && pwd_len < (int)kMaxPasswordLen) {
    out_password[pwd_len++] = *ptr++;
  }
  out_password[pwd_len] = '\0';
  
  // Validate both
  if (!isValidSSID(out_ssid)) {
    secureZeroMemory(out_ssid, kMaxSSIDLen + 1);
    secureZeroMemory(out_password, kMaxPasswordLen + 1);
    return false;
  }
  
  if (!isValidPassword(out_password)) {
    secureZeroMemory(out_ssid, kMaxSSIDLen + 1);
    secureZeroMemory(out_password, kMaxPasswordLen + 1);
    return false;
  }
  
  return true;
}

// ============================================================================
// Secure Memory Operations
// ============================================================================

void secureZeroMemory(char* buffer, size_t size) {
  if (!buffer || size == 0) return;
  
  // Use volatile to prevent compiler optimization
  volatile char* vptr = buffer;
  while (size--) {
    *vptr++ = '\0';
  }
}

// ============================================================================
// Diagnostics
// ============================================================================

void logWiFiConfigStatus() {
  if (hasCredentialsInNVS()) {
    Serial.println("[WiFiConfig] Credentials found in NVS");
  } else {
    Serial.println("[WiFiConfig] No persistent credentials, boot in AP mode");
  }
}

}  // namespace ZacusWiFiConfig
