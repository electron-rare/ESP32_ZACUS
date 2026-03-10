// app_registry.cpp - app catalog loading/parsing.
#include "app/app_registry.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "storage_manager.h"

namespace {

struct FallbackAppEntry {
  const char* id;
  const char* title;
  const char* category;
  const char* entry_screen;
  bool enabled;
  uint32_t required;
  uint32_t optional;
  bool offline;
  bool streaming;
  const char* manifest;
};

constexpr FallbackAppEntry kFallbackApps[] = {
    {"audio_player", "Lecteur Audio", "media", "SCENE_AUDIO_PLAYER", true,
     CAP_AUDIO_OUT | CAP_STORAGE_FS | CAP_GPU_UI, CAP_WIFI,
     true, true, "/apps/audio_player/manifest.json"},
    {"camera_video", "Appareil Photo/Video", "capture", "SCENE_PHOTO_MANAGER", true,
     CAP_CAMERA | CAP_STORAGE_FS | CAP_GPU_UI,
     CAP_STORAGE_SD, true, false, "/apps/camera_video/manifest.json"},
    {"dictaphone", "Dictaphone", "capture", "SCENE_RECORDER", true,
     CAP_AUDIO_IN | CAP_STORAGE_FS | CAP_GPU_UI, CAP_STORAGE_SD,
     true, false, "/apps/dictaphone/manifest.json"},
    {"timer_tools", "Chronometre/Minuteur", "utility", "SCENE_TIMER", true,
     CAP_GPU_UI, 0U, true, false,
     "/apps/timer_tools/manifest.json"},
    {"flashlight", "Lampe de Poche", "utility", "SCENE_FLASHLIGHT", true,
     CAP_LED | CAP_GPU_UI, 0U, true, false,
     "/apps/flashlight/manifest.json"},
    {"calculator", "Calculatrice", "utility", "SCENE_CALCULATOR", true,
     CAP_GPU_UI, 0U, true, false,
     "/apps/calculator/manifest.json"},
    {"kids_webradio", "Webradio Enfants", "kids_media", "SCENE_WEBRADIO", false,
     CAP_AUDIO_OUT | CAP_GPU_UI,
     CAP_WIFI | CAP_STORAGE_FS, true, true, "/apps/kids_webradio/manifest.json"},
    {"kids_podcast", "Podcast Enfants", "kids_media", "SCENE_PODCAST", false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true, "/apps/kids_podcast/manifest.json"},
    {"qr_scanner", "Lecteur QR Code", "capture", "SCENE_QR_DETECTOR", true,
     CAP_CAMERA | CAP_GPU_UI, 0U, true, false,
     "/apps/qr_scanner/manifest.json"},
    {"nes_emulator", "Emulateur NES", "games", "SCENE_NES_EMU", false,
     CAP_GPU_UI | CAP_AUDIO_OUT | CAP_STORAGE_FS, CAP_STORAGE_SD,
     true, false, "/apps/nes_emulator/manifest.json"},
    {"audiobook_player", "Livres Audio", "media", "SCENE_AUDIOBOOK", true,
     CAP_AUDIO_OUT | CAP_GPU_UI | CAP_STORAGE_FS,
     CAP_WIFI | CAP_STORAGE_SD, true, true, "/apps/audiobook_player/manifest.json"},
    {"kids_drawing", "Dessin", "kids_learning", "SCENE_DRAWING", false,
     CAP_GPU_UI, 0U, true, false,
     "/apps/kids_drawing/manifest.json"},
    {"kids_coloring", "Coloriage", "kids_learning", "SCENE_COLORING", false,
     CAP_GPU_UI, 0U, true, false,
     "/apps/kids_coloring/manifest.json"},
    {"kids_music", "Musique Enfants", "kids_media", "SCENE_KIDS_MUSIC", false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true, "/apps/kids_music/manifest.json"},
    {"kids_yoga", "Yoga Enfants", "kids_wellness", "SCENE_KIDS_YOGA", false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true, "/apps/kids_yoga/manifest.json"},
    {"kids_meditation", "Meditation Enfants", "kids_wellness", "SCENE_KIDS_MEDITATION", false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS, true, true, "/apps/kids_meditation/manifest.json"},
    {"kids_languages", "Langues Enfants", "kids_learning", "SCENE_KIDS_LANG", false,
     CAP_AUDIO_OUT | CAP_GPU_UI,
     CAP_WIFI | CAP_STORAGE_FS, true, true, "/apps/kids_languages/manifest.json"},
    {"kids_math", "Maths Enfants", "kids_learning", "SCENE_KIDS_MATH", false,
     CAP_GPU_UI, CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS,
     true, true, "/apps/kids_math/manifest.json"},
    {"kids_science", "Sciences Enfants", "kids_learning", "SCENE_KIDS_SCIENCE", false,
     CAP_GPU_UI,
     CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS, true, true, "/apps/kids_science/manifest.json"},
    {"kids_geography", "Geographie Enfants", "kids_learning", "SCENE_KIDS_GEO", false,
     CAP_GPU_UI,
     CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS, true, true, "/apps/kids_geography/manifest.json"},
};

bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  for (size_t i = 0U;; ++i) {
    const char a = lhs[i];
    const char b = rhs[i];
    if (a == '\0' && b == '\0') {
      return true;
    }
    if (a == '\0' || b == '\0') {
      return false;
    }
    if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) {
      return false;
    }
  }
}

}  // namespace

bool AppRegistry::loadFromFs(const StorageManager& storage, const char* registry_path) {
  descriptors_.clear();
  const char* path = (registry_path != nullptr && registry_path[0] != '\0') ? registry_path : "/apps/registry.json";
  const String payload = storage.loadTextFile(path);
  if (payload.isEmpty() || !loadFromJson(payload.c_str())) {
    loadFallbackCatalog();
    return false;
  }
  if (descriptors_.empty()) {
    loadFallbackCatalog();
    return false;
  }
  return true;
}

const AppDescriptor* AppRegistry::find(const char* id) const {
  if (id == nullptr || id[0] == '\0') {
    return nullptr;
  }
  for (const AppDescriptor& descriptor : descriptors_) {
    if (equalsIgnoreCase(descriptor.id, id)) {
      return &descriptor;
    }
  }
  return nullptr;
}

std::vector<AppDescriptor> AppRegistry::listByCategory(const char* category) const {
  std::vector<AppDescriptor> filtered;
  if (category == nullptr || category[0] == '\0') {
    return descriptors_;
  }
  for (const AppDescriptor& descriptor : descriptors_) {
    if (equalsIgnoreCase(descriptor.category, category)) {
      filtered.push_back(descriptor);
    }
  }
  return filtered;
}

const std::vector<AppDescriptor>& AppRegistry::descriptors() const {
  return descriptors_;
}

bool AppRegistry::loadFromJson(const char* json_text) {
  if (json_text == nullptr || json_text[0] == '\0') {
    return false;
  }
  DynamicJsonDocument document(8192);
  const DeserializationError error = deserializeJson(document, json_text);
  if (error) {
    return false;
  }
  JsonArrayConst items;
  if (document.is<JsonArrayConst>()) {
    items = document.as<JsonArrayConst>();
  } else if (document["apps"].is<JsonArrayConst>()) {
    items = document["apps"].as<JsonArrayConst>();
  } else {
    return false;
  }
  descriptors_.clear();
  for (JsonVariantConst item : items) {
    if (!item.is<JsonObjectConst>()) {
      continue;
    }
    JsonObjectConst entry = item.as<JsonObjectConst>();
    const char* id = entry["id"] | "";
    if (id[0] == '\0') {
      continue;
    }
    AppDescriptor descriptor = {};
    copyText(descriptor.id, sizeof(descriptor.id), id);
    copyText(descriptor.title, sizeof(descriptor.title), entry["title"] | id);
    copyText(descriptor.category, sizeof(descriptor.category), entry["category"] | "misc");
    copyText(descriptor.entry_screen, sizeof(descriptor.entry_screen), entry["entry_screen"] | "");
    descriptor.enabled = entry["enabled"] | true;
    copyText(descriptor.version, sizeof(descriptor.version), entry["version"] | "1.0.0");
    copyText(descriptor.icon_path, sizeof(descriptor.icon_path), entry["icon_path"] | "");
    if (descriptor.icon_path[0] == '\0') {
      char default_icon[96] = {0};
      std::snprintf(default_icon, sizeof(default_icon), "/apps/%s/icon.png", descriptor.id);
      copyText(descriptor.icon_path, sizeof(descriptor.icon_path), default_icon);
    }
    copyText(descriptor.asset_manifest, sizeof(descriptor.asset_manifest), entry["asset_manifest"] | "");
    descriptor.supports_offline = entry["supports_offline"] | true;
    descriptor.supports_streaming = entry["supports_streaming"] | false;
    if (entry["required_capabilities"].is<uint32_t>()) {
      descriptor.required_capabilities = entry["required_capabilities"].as<uint32_t>();
    } else if (entry["required_capabilities"].is<const char*>()) {
      descriptor.required_capabilities = parseCapabilityMask(entry["required_capabilities"] | "");
    } else {
      descriptor.required_capabilities = parseCapabilityMask(entry["required_caps"] | "");
    }
    if (entry["optional_capabilities"].is<uint32_t>()) {
      descriptor.optional_capabilities = entry["optional_capabilities"].as<uint32_t>();
    } else if (entry["optional_capabilities"].is<const char*>()) {
      descriptor.optional_capabilities = parseCapabilityMask(entry["optional_capabilities"] | "");
    } else {
      descriptor.optional_capabilities = parseCapabilityMask(entry["optional_caps"] | "");
    }
    descriptors_.push_back(descriptor);
  }
  return !descriptors_.empty();
}

void AppRegistry::loadFallbackCatalog() {
  descriptors_.clear();
  descriptors_.reserve(sizeof(kFallbackApps) / sizeof(kFallbackApps[0]));
  for (const FallbackAppEntry& fallback : kFallbackApps) {
    AppDescriptor descriptor = {};
    copyText(descriptor.id, sizeof(descriptor.id), fallback.id);
    copyText(descriptor.title, sizeof(descriptor.title), fallback.title);
    copyText(descriptor.category, sizeof(descriptor.category), fallback.category);
    copyText(descriptor.entry_screen, sizeof(descriptor.entry_screen), fallback.entry_screen);
    descriptor.enabled = fallback.enabled;
    copyText(descriptor.version, sizeof(descriptor.version), "1.0.0");
    char icon_path[96] = {0};
    std::snprintf(icon_path, sizeof(icon_path), "/apps/%s/icon.png", fallback.id);
    copyText(descriptor.icon_path, sizeof(descriptor.icon_path), icon_path);
    copyText(descriptor.asset_manifest, sizeof(descriptor.asset_manifest), fallback.manifest);
    descriptor.required_capabilities = fallback.required;
    descriptor.optional_capabilities = fallback.optional;
    descriptor.supports_offline = fallback.offline;
    descriptor.supports_streaming = fallback.streaming;
    descriptors_.push_back(descriptor);
  }
}

uint32_t AppRegistry::parseCapabilityMask(const char* csv_caps) {
  if (csv_caps == nullptr || csv_caps[0] == '\0') {
    return 0U;
  }
  char buffer[192] = {0};
  copyText(buffer, sizeof(buffer), csv_caps);
  uint32_t mask = 0U;
  char* token = std::strtok(buffer, ",| ");
  while (token != nullptr) {
    for (size_t i = 0U; token[i] != '\0'; ++i) {
      token[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
    }
    if (std::strcmp(token, "cap_audio_out") == 0 || std::strcmp(token, "audio_out") == 0) {
      mask |= CAP_AUDIO_OUT;
    } else if (std::strcmp(token, "cap_audio_in") == 0 || std::strcmp(token, "audio_in") == 0) {
      mask |= CAP_AUDIO_IN;
    } else if (std::strcmp(token, "cap_camera") == 0 || std::strcmp(token, "camera") == 0) {
      mask |= CAP_CAMERA;
    } else if (std::strcmp(token, "cap_led") == 0 || std::strcmp(token, "led") == 0) {
      mask |= CAP_LED;
    } else if (std::strcmp(token, "cap_wifi") == 0 || std::strcmp(token, "wifi") == 0) {
      mask |= CAP_WIFI;
    } else if (std::strcmp(token, "cap_storage_sd") == 0 || std::strcmp(token, "storage_sd") == 0) {
      mask |= CAP_STORAGE_SD;
    } else if (std::strcmp(token, "cap_storage_fs") == 0 || std::strcmp(token, "storage_fs") == 0) {
      mask |= CAP_STORAGE_FS;
    } else if (std::strcmp(token, "cap_gpu_ui") == 0 || std::strcmp(token, "gpu_ui") == 0) {
      mask |= CAP_GPU_UI;
    }
    token = std::strtok(nullptr, ",| ");
  }
  return mask;
}

void AppRegistry::copyText(char* out, size_t out_size, const char* text) {
  if (out == nullptr || out_size == 0U) {
    return;
  }
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  std::strncpy(out, text, out_size - 1U);
  out[out_size - 1U] = '\0';
}
