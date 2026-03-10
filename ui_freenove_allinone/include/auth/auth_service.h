#pragma once

#include <cstdint>
#include <cstddef>

namespace AuthService {

// Token length (32 hex chars)
constexpr size_t kTokenLength = 32;
constexpr size_t kTokenBufferSize = kTokenLength + 1;

// Status codes for token validation
enum class AuthStatus {
  kOk,              // Token valid
  kMissingHeader,   // No Authorization header provided
  kInvalidFormat,   // Not "Bearer <token>" format
  kInvalidToken,    // Token doesn't match stored token
  kUninitialized,   // Service not initialized (init() not called)
  kInternalError,   // NVS read/write error
};

// ============================================================================
// Initialize auth service
// Call from setup() before setting up WebServer handlers
// Generates random token if first boot, or loads from NVS if persisted
// ============================================================================
bool init();

// ============================================================================
// Validate Bearer token from HTTP Authorization header
// 
// Usage:
//   const char* auth_header = g_web_server.header("Authorization").c_str();
//   AuthStatus status = validateBearerToken(auth_header);
//   if (status != AuthStatus::kOk) {
//     // Return 401 with error message
//     g_web_server.send(401, "application/json", 
//       "{\"ok\":false,\"error\":\"unauthorized\"}");
//     return;
//   }
//   // Request is authenticated, proceed
// ============================================================================
AuthStatus validateBearerToken(const char* auth_header);

// ============================================================================
// Get current active Bearer token
// buffer must be at least kTokenBufferSize bytes
// Returns true if token retrieved, false if service not initialized
// ============================================================================
bool getCurrentToken(char* out_token_buffer, size_t buffer_size);

// ============================================================================
// Rotate token - generate new random token and persist to NVS
// Call this via serial command or periodically for security rotation
// Returns new token in out_new_token_buffer if successful
// ============================================================================
bool rotateToken(char* out_new_token_buffer, size_t buffer_size);

// ============================================================================
// Reset auth service - clear NVS and generate new fresh token
// Used when token compromised or need full reset
// ============================================================================
bool reset();

// ============================================================================
// Get human-readable error message for AuthStatus
// Useful for logging and JSON error responses
// ============================================================================
const char* statusMessage(AuthStatus status);

// ============================================================================
// Query if authentication is enabled
// Can be disabled for testing/debugging via setEnabled(false)
// ============================================================================
bool isEnabled();

// ============================================================================
// Enable or disable authentication globally
// WARNING: Only disable for testing! Do not ship with auth disabled!
// ============================================================================
void setEnabled(bool enabled);

}  // namespace AuthService
