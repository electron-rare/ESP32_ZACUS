// app_registry.h - App catalog loaded from registry.json.
#pragma once

#include <Arduino.h>

struct AppEntry {
  char id[24] = {0};
  char title[32] = {0};
  char category[16] = {0};
  char icon_path[48] = {0};
  char entry_screen[40] = {0};
  bool enabled = true;
  uint8_t required_capabilities = 0U;  // bitmask (lightweight)
};

// Capability bits for required_capabilities bitmask.
namespace AppCap {
constexpr uint8_t kAudioOut   = 0x01U;
constexpr uint8_t kAudioIn    = 0x02U;
constexpr uint8_t kStorageFs  = 0x04U;
constexpr uint8_t kStorageSd  = 0x08U;
constexpr uint8_t kCamera     = 0x10U;
constexpr uint8_t kWifi       = 0x20U;
constexpr uint8_t kGpuUi      = 0x40U;
}  // namespace AppCap

class AppRegistry {
 public:
  static constexpr uint8_t kMaxApps = 24U;

  AppRegistry() = default;

  bool loadFromJson(const char* json_path);
  bool loadFromBuffer(const char* json_buffer, size_t len);

  uint8_t count() const { return count_; }
  const AppEntry* entry(uint8_t index) const;
  const AppEntry* findById(const char* id) const;

  uint8_t enabledCount() const;
  const AppEntry* enabledEntry(uint8_t visible_index) const;

 private:
  AppEntry entries_[kMaxApps];
  uint8_t count_ = 0U;
};
