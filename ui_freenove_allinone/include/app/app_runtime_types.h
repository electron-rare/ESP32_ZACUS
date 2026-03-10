// app_runtime_types.h - contracts for app catalog/runtime orchestration.
#pragma once

#include <Arduino.h>

#include <cstdint>

class AudioManager;
class CameraManager;
class HardwareManager;
class MediaManager;
class NetworkManager;
class StorageManager;
class UiManager;

namespace runtime::resource {
class ResourceCoordinator;
}

enum AppCapability : uint32_t {
  CAP_AUDIO_OUT = 1UL << 0,
  CAP_AUDIO_IN = 1UL << 1,
  CAP_CAMERA = 1UL << 2,
  CAP_LED = 1UL << 3,
  CAP_WIFI = 1UL << 4,
  CAP_STORAGE_SD = 1UL << 5,
  CAP_STORAGE_FS = 1UL << 6,
  CAP_GPU_UI = 1UL << 7,
};

inline bool appCapabilityMaskHas(uint32_t mask, AppCapability cap) {
  return (mask & static_cast<uint32_t>(cap)) != 0U;
}

struct AppDescriptor {
  char id[40] = "";
  char title[48] = "";
  char category[24] = "";
  char entry_screen[40] = "";
  bool enabled = true;
  char version[16] = "1.0.0";
  char icon_path[96] = "";
  uint32_t required_capabilities = 0U;
  uint32_t optional_capabilities = 0U;
  bool supports_offline = true;
  bool supports_streaming = false;
  char asset_manifest[96] = "";
};

enum class AppRuntimeState : uint8_t {
  kIdle = 0,
  kStarting,
  kRunning,
  kStopping,
  kFailed,
};

struct AppStartRequest {
  char id[40] = "";
  char mode[16] = "default";
  char source[24] = "api";
};

struct AppStopRequest {
  char id[40] = "";
  char reason[24] = "api";
};

struct AppAction {
  char id[40] = "";
  char name[32] = "";
  char payload[512] = "";
  char content_type[24] = "text/plain";
};

struct AppRuntimeStatus {
  char id[40] = "";
  AppRuntimeState state = AppRuntimeState::kIdle;
  char mode[16] = "default";
  char source[24] = "n/a";
  char last_error[64] = "";
  char last_event[40] = "";
  uint32_t started_at_ms = 0U;
  uint32_t last_tick_ms = 0U;
  uint32_t tick_count = 0U;
  uint32_t required_cap_mask = 0U;
  uint32_t missing_cap_mask = 0U;
  uint32_t avg_tick_us = 0U;
  uint32_t max_tick_us = 0U;
  uint32_t heap_free = 0U;
  uint32_t psram_free = 0U;
};

struct AppContext {
  const AppDescriptor* descriptor = nullptr;
  AudioManager* audio = nullptr;
  CameraManager* camera = nullptr;
  HardwareManager* hardware = nullptr;
  MediaManager* media = nullptr;
  NetworkManager* network = nullptr;
  StorageManager* storage = nullptr;
  UiManager* ui = nullptr;
  runtime::resource::ResourceCoordinator* resource = nullptr;
};

class IAppModule {
 public:
  virtual ~IAppModule() = default;
  virtual bool begin(const AppContext& context) = 0;
  virtual void tick(uint32_t now_ms) = 0;
  virtual void handleAction(const AppAction& action) = 0;
  virtual void end() = 0;
  virtual AppRuntimeStatus status() const = 0;
};
