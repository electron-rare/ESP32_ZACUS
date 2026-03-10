#pragma once
#include <Arduino.h>
#include <cstring>

namespace ZacusWiFiConfig {

// ============================================================================
// Constants & Limits
// ============================================================================
constexpr const char kNvsNamespace[] = "zacus_wifi";
constexpr const char kNvsKeySSID[] = "wifi_ssid";
constexpr const char kNvsKeyPassword[] = "wifi_pwd";
constexpr const char kNvsKeyIsSet[] = "wifi_set";  // 1 = credentials saved
constexpr size_t kMaxSSIDLen = 32;
constexpr size_t kMaxPasswordLen = 64;
constexpr size_t kSerialCmdMaxLen = 128;

// ============================================================================
// Validation
// ============================================================================

/**
 * @brief Validate SSID format
 * - Not empty
 * - Max 32 chars
 * - No null terminators in middle
 */
bool isValidSSID(const char* ssid);

/**
 * @brief Validate password strength
 * - If present, minimum 8 chars (WPA2 requirement)
 * - Max 64 chars
 * - No control characters
 */
bool isValidPassword(const char* password);

// ============================================================================
// NVS Read/Write Operations
// ============================================================================

/**
 * @brief Read WiFi SSID from NVS
 * @param out_ssid Buffer to fill (must be >= 33 bytes)
 * @param max_len Max length to read
 * @return true if read successfully, false if not found/error
 */
bool readSSIDFromNVS(char* out_ssid, size_t max_len);

/**
 * @brief Read WiFi password from NVS
 * @param out_password Buffer to fill (must be >= 65 bytes)
 * @param max_len Max length to read
 * @return true if read successfully, false if not found/error
 */
bool readPasswordFromNVS(char* out_password, size_t max_len);

/**
 * @brief Write WiFi SSID to NVS (persistent storage)
 * @param ssid SSID to store (validated)
 * @return true if write successful
 */
bool writeSSIDToNVS(const char* ssid);

/**
 * @brief Write WiFi password to NVS (persistent storage)
 * @param password Password to store (validated)
 * @return true if write successful
 */
bool writePasswordToNVS(const char* password);

/**
 * @brief Check if credentials are stored in NVS
 * @return true if at least SSID is saved
 */
bool hasCredentialsInNVS();

// ============================================================================
// Configuration Reset
// ============================================================================

/**
 * @brief Clear all WiFi credentials from NVS (factory reset)
 * @return true if successful
 */
bool clearNVSWiFiConfig();

// ============================================================================
// Serial Command Parsing
// ============================================================================

/**
 * @brief Parse a WiFi configuration command from serial
 * Format: "WIFI_CONFIG <SSID> <PASSWORD>"
 * Example: "WIFI_CONFIG MyNetwork MyPassword123"
 *
 * @param cmd_line Input command string
 * @param out_ssid Output SSID (buffer >= 33 bytes)
 * @param out_password Output password (buffer >= 65 bytes)
 * @return true if parsed successfully and values are valid
 */
bool parseWifiConfigCommand(const char* cmd_line,
                           char* out_ssid,
                           char* out_password);

// ============================================================================
// Secure Memory Operations
// ============================================================================

/**
 * @brief Securely zero memory containing sensitive data
 * Prevents compiler optimization from eliminating memset
 * @param buffer Buffer to clear
 * @param size Size in bytes
 */
void secureZeroMemory(char* buffer, size_t size);

// ============================================================================
// Diagnostics
// ============================================================================

/**
 * @brief Log WiFi configuration status (safe: no passwords printed)
 */
void logWiFiConfigStatus();

}  // namespace ZacusWiFiConfig
