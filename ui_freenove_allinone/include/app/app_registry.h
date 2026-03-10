// app_registry.h - app catalog registry with legacy AppEntry compatibility.
#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "app/app_runtime_types.h"

class StorageManager;

struct AppEntry {
  char id[24] = {0};
  char title[32] = {0};
  char category[16] = {0};
  char icon_path[48] = {0};
  char entry_screen[40] = {0};
  bool enabled = true;
  uint32_t required_capabilities = 0U;
};

class AppRegistry {
 public:
  static constexpr uint8_t kMaxApps = 24U;

  AppRegistry() = default;

  // New API used by runtime manager / web contracts.
  bool loadFromFs(const StorageManager& storage, const char* registry_path = "/apps/registry.json");
  bool loadFromJson(const char* json_text);
  bool loadFromBuffer(const char* json_buffer, size_t len);

  const AppDescriptor* find(const char* id) const;
  std::vector<AppDescriptor> listByCategory(const char* category) const;
  const std::vector<AppDescriptor>& descriptors() const;

  // Legacy API used by workbench UI modules.
  uint8_t count() const { return count_; }
  const AppEntry* entry(uint8_t index) const;
  const AppEntry* findById(const char* id) const;
  uint8_t enabledCount() const;
  const AppEntry* enabledEntry(uint8_t visible_index) const;

 private:
  void loadFallbackCatalog();
  static uint32_t parseCapabilityMask(const char* csv_caps);
  void rebuildLegacyEntries();

  std::vector<AppDescriptor> descriptors_;
  AppEntry entries_[kMaxApps] = {};
  uint8_t count_ = 0U;
};
