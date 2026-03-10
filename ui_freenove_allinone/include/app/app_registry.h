// app_registry.h - app catalog loaded from LittleFS with embedded fallback.
#pragma once

#include <vector>

#include "app/app_runtime_types.h"

class StorageManager;

class AppRegistry {
 public:
  bool loadFromFs(const StorageManager& storage, const char* registry_path = "/apps/registry.json");
  const AppDescriptor* find(const char* id) const;
  std::vector<AppDescriptor> listByCategory(const char* category) const;
  const std::vector<AppDescriptor>& descriptors() const;

 private:
  bool loadFromJson(const char* json_text);
  void loadFallbackCatalog();
  static uint32_t parseCapabilityMask(const char* csv_caps);
  static void copyText(char* out, size_t out_size, const char* text);

  std::vector<AppDescriptor> descriptors_;
};

