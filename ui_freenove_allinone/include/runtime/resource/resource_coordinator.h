// resource_coordinator.h - runtime arbitration between UI graphics and optional camera/mic workloads.
#pragma once

#include <cstdint>

struct UiMemorySnapshot;

namespace runtime::resource {

enum class ResourceCapability : uint32_t {
  kAudioOut = 1UL << 0,
  kAudioIn = 1UL << 1,
  kCamera = 1UL << 2,
  kLed = 1UL << 3,
  kWifi = 1UL << 4,
  kStorageSd = 1UL << 5,
  kStorageFs = 1UL << 6,
  kGpuUi = 1UL << 7,
};

enum class ResourceProfile : uint8_t {
  kGfxFocus = 0,
  kGfxPlusMic,
  kGfxPlusCamSnapshot,
};

struct ResourceCoordinatorConfig {
  uint32_t flush_overflow_delta_threshold = 2U;
  uint32_t flush_blocked_delta_threshold = 24U;
  uint32_t draw_max_us_threshold = 26000U;
  uint32_t flush_max_us_threshold = 42000U;
  uint32_t pressure_hold_ms = 1800U;
  uint32_t mic_hold_ms = 1200U;
  uint32_t camera_cooldown_ms = 900U;
};

struct ResourceCoordinatorSnapshot {
  ResourceProfile profile = ResourceProfile::kGfxFocus;
  bool graphics_pressure = false;
  bool mic_should_run = false;
  bool mic_force_on = false;
  bool allow_camera_ops = false;
  uint32_t now_ms = 0U;
  uint32_t pressure_until_ms = 0U;
  uint32_t mic_hold_until_ms = 0U;
  uint32_t camera_cooldown_until_ms = 0U;
  uint32_t camera_allowed_ops = 0U;
  uint32_t camera_blocked_ops = 0U;
  uint32_t flush_overflow_delta = 0U;
  uint32_t flush_blocked_delta = 0U;
  uint32_t last_draw_avg_us = 0U;
  uint32_t last_draw_max_us = 0U;
  uint32_t last_flush_avg_us = 0U;
  uint32_t last_flush_max_us = 0U;
};

class ResourceCoordinator {
 public:
  ResourceCoordinator() = default;

  void begin(const ResourceCoordinatorConfig& config = ResourceCoordinatorConfig());
  void setProfile(ResourceProfile profile);
  ResourceProfile profile() const;
  const char* profileName() const;
  bool parseAndSetProfile(const char* token);
  static const char* profileName(ResourceProfile profile);
  static bool parseProfile(const char* token, ResourceProfile* out_profile);

  void update(const UiMemorySnapshot& ui_snapshot, uint32_t now_ms);
  bool shouldRunMic() const;
  bool shouldForceMicOn() const;
  bool allowsCameraWork() const;
  bool approveCameraOperation();
  bool allowsCapability(ResourceCapability capability) const;
  uint32_t capabilityMask() const;
  static const char* capabilityName(ResourceCapability capability);
  ResourceCoordinatorSnapshot snapshot() const;

 private:
  ResourceCoordinatorConfig config_ = {};
  ResourceCoordinatorSnapshot snapshot_ = {};
  uint32_t prev_flush_overflow_ = 0U;
  uint32_t prev_flush_blocked_ = 0U;
};

}  // namespace runtime::resource
