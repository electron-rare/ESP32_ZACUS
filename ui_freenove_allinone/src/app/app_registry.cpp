// app_registry.cpp - app catalog loading/parsing.
#include "app/app_registry.h"

#include <ArduinoJson.h>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <vector>

#include "core/str_utils.h"
#include "storage_manager.h"

namespace {

struct FallbackAppEntry {
  const char* id;
  const char* title;
  const char* category;
  const char* entry_screen;
  bool        enabled;
  uint32_t    required;
  uint32_t    optional;
  bool        offline;
  bool        streaming;
  const char* manifest;
};

constexpr FallbackAppEntry kFallbackApps[] = {
    {"audio_player",     "Lecteur Audio",         "media",         "SCENE_AUDIO_PLAYER",    true,
     CAP_AUDIO_OUT | CAP_STORAGE_FS | CAP_GPU_UI, CAP_WIFI,
     true, true,  "/apps/audio_player/manifest.json"},
    {"camera_video",     "Appareil Photo/Video",  "capture",       "SCENE_PHOTO_MANAGER",   true,
     CAP_CAMERA | CAP_STORAGE_FS | CAP_GPU_UI,    CAP_STORAGE_SD,
     true, false, "/apps/camera_video/manifest.json"},
    {"dictaphone",       "Dictaphone",            "capture",       "SCENE_RECORDER",        true,
     CAP_AUDIO_IN | CAP_STORAGE_FS | CAP_GPU_UI,  CAP_STORAGE_SD,
     true, false, "/apps/dictaphone/manifest.json"},
    {"timer_tools",      "Chronometre/Minuteur",  "utility",       "SCENE_TIMER",           true,
     CAP_GPU_UI, 0U, true, false, "/apps/timer_tools/manifest.json"},
    {"flashlight",       "Lampe de Poche",        "utility",       "SCENE_FLASHLIGHT",      true,
     CAP_LED | CAP_GPU_UI, 0U, true, false, "/apps/flashlight/manifest.json"},
    {"calculator",       "Calculatrice",          "utility",       "SCENE_CALCULATOR",      true,
     CAP_GPU_UI, 0U, true, false, "/apps/calculator/manifest.json"},
    {"kids_webradio",    "Webradio Enfants",      "kids_media",    "SCENE_WEBRADIO",        false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_webradio/manifest.json"},
    {"kids_podcast",     "Podcast Enfants",       "kids_media",    "SCENE_PODCAST",         false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_podcast/manifest.json"},
    {"qr_scanner",       "Lecteur QR Code",       "capture",       "SCENE_QR_DETECTOR",     true,
     CAP_CAMERA | CAP_GPU_UI, 0U, true, false, "/apps/qr_scanner/manifest.json"},
    {"nes_emulator",     "Emulateur NES",         "games",         "SCENE_NES_EMU",         false,
     CAP_GPU_UI | CAP_AUDIO_OUT | CAP_STORAGE_FS, CAP_STORAGE_SD,
     true, false, "/apps/nes_emulator/manifest.json"},
    {"audiobook_player", "Livres Audio",          "media",         "SCENE_AUDIOBOOK",       true,
     CAP_AUDIO_OUT | CAP_GPU_UI | CAP_STORAGE_FS, CAP_WIFI | CAP_STORAGE_SD,
     true, true,  "/apps/audiobook_player/manifest.json"},
    {"kids_drawing",     "Dessin",                "kids_learning", "SCENE_DRAWING",         false,
     CAP_GPU_UI, 0U, true, false, "/apps/kids_drawing/manifest.json"},
    {"kids_coloring",    "Coloriage",             "kids_learning", "SCENE_COLORING",        false,
     CAP_GPU_UI, 0U, true, false, "/apps/kids_coloring/manifest.json"},
    {"kids_music",       "Musique Enfants",       "kids_media",    "SCENE_KIDS_MUSIC",      false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_music/manifest.json"},
    {"kids_yoga",        "Yoga Enfants",          "kids_wellness", "SCENE_KIDS_YOGA",       false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_yoga/manifest.json"},
    {"kids_meditation",  "Meditation Enfants",    "kids_wellness", "SCENE_KIDS_MEDITATION", false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_meditation/manifest.json"},
    {"kids_languages",   "Langues Enfants",       "kids_learning", "SCENE_KIDS_LANG",       false,
     CAP_AUDIO_OUT | CAP_GPU_UI, CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_languages/manifest.json"},
    {"kids_math",        "Maths Enfants",         "kids_learning", "SCENE_KIDS_MATH",       false,
     CAP_GPU_UI, CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_math/manifest.json"},
    {"kids_science",     "Sciences Enfants",      "kids_learning", "SCENE_KIDS_SCIENCE",    false,
     CAP_GPU_UI, CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_science/manifest.json"},
    {"kids_geography",   "Geographie Enfants",    "kids_learning", "SCENE_KIDS_GEO",        false,
     CAP_GPU_UI, CAP_AUDIO_OUT | CAP_WIFI | CAP_STORAGE_FS,
     true, true,  "/apps/kids_geography/manifest.json"},
};

}  // namespace

// ============================================================================
// Public API
// ============================================================================

bool AppRegistry::loadFromFs(const StorageManager& storage, const char* registry_path) {
  descriptors_.clear();
  const char* path = (registry_path != nullptr && registry_path[0] != '\0')
                         ? registry_path
                         : "/apps/registry.json";
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

bool AppRegistry::loadFromBuffer(const char* json_buffer, size_t len) {
  (void)len;
  return loadFromJson(json_buffer);
}

const AppDescriptor* AppRegistry::find(const char* id) const {
  if (id == nullptr || id[0] == '\0') return nullptr;
  for (const AppDescriptor& d : descriptors_) {
    if (core::equalsIgnoreCase(d.id, id)) return &d;
  }
  return nullptr;
}

std::vector<AppDescriptor> AppRegistry::listByCategory(const char* category) const {
  if (category == nullptr || category[0] == '\0') return descriptors_;
  std::vector<AppDescriptor> filtered;
  for (const AppDescriptor& d : descriptors_) {
    if (core::equalsIgnoreCase(d.category, category)) filtered.push_back(d);
  }
  return filtered;
}

const std::vector<AppDescriptor>& AppRegistry::descriptors() const {
  return descriptors_;
}

// ============================================================================
// JSON parsing — called once at boot. DynamicJsonDocument(8192) intentional.
// ============================================================================

bool AppRegistry::loadFromJson(const char* json_text) {
  if (json_text == nullptr || json_text[0] == '\0') return false;

  DynamicJsonDocument document(8192);
  if (deserializeJson(document, json_text) != DeserializationError::Ok) {
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
    if (!item.is<JsonObjectConst>()) continue;
    JsonObjectConst entry = item.as<JsonObjectConst>();

    const char* id = entry["id"] | "";
    if (id[0] == '\0') continue;

    AppDescriptor d = {};
    core::copyText(d.id,           sizeof(d.id),           id);
    core::copyText(d.title,        sizeof(d.title),        entry["title"]        | id);
    core::copyText(d.category,     sizeof(d.category),     entry["category"]     | "misc");
    core::copyText(d.entry_screen, sizeof(d.entry_screen), entry["entry_screen"] | "");
    d.enabled = entry["enabled"] | true;
    core::copyText(d.version,      sizeof(d.version),      entry["version"]      | "1.0.0");

    core::copyText(d.icon_path, sizeof(d.icon_path), entry["icon_path"] | "");
    if (d.icon_path[0] == '\0') {
      char default_icon[96] = {};
      std::snprintf(default_icon, sizeof(default_icon), "/apps/%s/icon.png", d.id);
      core::copyText(d.icon_path, sizeof(d.icon_path), default_icon);
    }

    core::copyText(d.asset_manifest, sizeof(d.asset_manifest), entry["asset_manifest"] | "");
    d.supports_offline   = entry["supports_offline"]   | true;
    d.supports_streaming = entry["supports_streaming"] | false;

    // Capabilities: accept uint32 or CSV string, with legacy key aliases.
    auto parseCap = [&](const char* key, const char* key_alt) -> uint32_t {
      if (entry[key].is<uint32_t>())    return entry[key].as<uint32_t>();
      if (entry[key].is<const char*>()) return parseCapabilityMask(entry[key] | "");
      return parseCapabilityMask(entry[key_alt] | "");
    };
    d.required_capabilities = parseCap("required_capabilities", "required_caps");
    d.optional_capabilities = parseCap("optional_capabilities", "optional_caps");

    descriptors_.push_back(d);
  }
  rebuildLegacyEntries();
  return !descriptors_.empty();
}

void AppRegistry::loadFallbackCatalog() {
  descriptors_.clear();
  descriptors_.reserve(sizeof(kFallbackApps) / sizeof(kFallbackApps[0]));
  for (const FallbackAppEntry& f : kFallbackApps) {
    AppDescriptor d = {};
    core::copyText(d.id,             sizeof(d.id),             f.id);
    core::copyText(d.title,          sizeof(d.title),          f.title);
    core::copyText(d.category,       sizeof(d.category),       f.category);
    core::copyText(d.entry_screen,   sizeof(d.entry_screen),   f.entry_screen);
    d.enabled = f.enabled;
    core::copyText(d.version,        sizeof(d.version),        "1.0.0");

    char icon_path[96] = {};
    std::snprintf(icon_path, sizeof(icon_path), "/apps/%s/icon.png", f.id);
    core::copyText(d.icon_path,      sizeof(d.icon_path),      icon_path);
    core::copyText(d.asset_manifest, sizeof(d.asset_manifest), f.manifest);

    d.required_capabilities = f.required;
    d.optional_capabilities = f.optional;
    d.supports_offline      = f.offline;
    d.supports_streaming    = f.streaming;
    descriptors_.push_back(d);
  }
  rebuildLegacyEntries();
}

uint32_t AppRegistry::parseCapabilityMask(const char* csv_caps) {
  if (csv_caps == nullptr || csv_caps[0] == '\0') return 0U;

  char buffer[192] = {};
  core::copyText(buffer, sizeof(buffer), csv_caps);

  uint32_t mask = 0U;
  char* token = std::strtok(buffer, ",| ");
  while (token != nullptr) {
    for (size_t i = 0U; token[i] != '\0'; ++i) {
      token[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
    }
    if      (std::strcmp(token, "cap_audio_out")  == 0 || std::strcmp(token, "audio_out")  == 0) mask |= CAP_AUDIO_OUT;
    else if (std::strcmp(token, "cap_audio_in")   == 0 || std::strcmp(token, "audio_in")   == 0) mask |= CAP_AUDIO_IN;
    else if (std::strcmp(token, "cap_camera")     == 0 || std::strcmp(token, "camera")     == 0) mask |= CAP_CAMERA;
    else if (std::strcmp(token, "cap_led")        == 0 || std::strcmp(token, "led")        == 0) mask |= CAP_LED;
    else if (std::strcmp(token, "cap_wifi")       == 0 || std::strcmp(token, "wifi")       == 0) mask |= CAP_WIFI;
    else if (std::strcmp(token, "cap_storage_sd") == 0 || std::strcmp(token, "storage_sd") == 0) mask |= CAP_STORAGE_SD;
    else if (std::strcmp(token, "cap_storage_fs") == 0 || std::strcmp(token, "storage_fs") == 0) mask |= CAP_STORAGE_FS;
    else if (std::strcmp(token, "cap_gpu_ui")     == 0 || std::strcmp(token, "gpu_ui")     == 0) mask |= CAP_GPU_UI;
    token = std::strtok(nullptr, ",| ");
  }
  return mask;
}

const AppEntry* AppRegistry::entry(uint8_t index) const {
  if (index >= count_) return nullptr;
  return &entries_[index];
}

const AppEntry* AppRegistry::findById(const char* id) const {
  if (id == nullptr || id[0] == '\0') return nullptr;
  for (uint8_t i = 0U; i < count_; ++i) {
    if (core::equalsIgnoreCase(entries_[i].id, id)) return &entries_[i];
  }
  return nullptr;
}

uint8_t AppRegistry::enabledCount() const {
  uint8_t enabled = 0U;
  for (uint8_t i = 0U; i < count_; ++i) {
    if (entries_[i].enabled) enabled += 1U;
  }
  return enabled;
}

const AppEntry* AppRegistry::enabledEntry(uint8_t visible_index) const {
  uint8_t seen = 0U;
  for (uint8_t i = 0U; i < count_; ++i) {
    if (!entries_[i].enabled) continue;
    if (seen == visible_index) return &entries_[i];
    seen += 1U;
  }
  return nullptr;
}

void AppRegistry::rebuildLegacyEntries() {
  count_ = 0U;
  std::memset(entries_, 0, sizeof(entries_));
  for (const AppDescriptor& d : descriptors_) {
    if (count_ >= kMaxApps) break;
    AppEntry& out = entries_[count_++];
    core::copyText(out.id, sizeof(out.id), d.id);
    core::copyText(out.title, sizeof(out.title), d.title);
    core::copyText(out.category, sizeof(out.category), d.category);
    core::copyText(out.icon_path, sizeof(out.icon_path), d.icon_path);
    core::copyText(out.entry_screen, sizeof(out.entry_screen), d.entry_screen);
    out.enabled = d.enabled;
    out.required_capabilities = d.required_capabilities;
  }
}
