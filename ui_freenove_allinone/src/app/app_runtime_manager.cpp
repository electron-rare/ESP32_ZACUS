// app_runtime_manager.cpp - app runtime control and lightweight module behavior.
#include "app/app_runtime_manager.h"

#include <cstdlib>

#include "app/modules/app_modules.h"
#include "audio_manager.h"
#include "camera_manager.h"
#include "core/str_utils.h"
#include "hardware_manager.h"
#include "media_manager.h"
#include "network_manager.h"
#include "runtime/resource/resource_coordinator.h"
#include "storage_manager.h"
#include "ui_manager.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace {

// ============================================================================
// Heap snapshot cache — ESP.getFreeHeap() is expensive; refresh every 1 s.
// ============================================================================

struct HeapCache {
  uint32_t heap_free  = 0U;
  uint32_t psram_free = 0U;
  uint32_t last_ms    = 0U;

  void refresh() {
    const uint32_t now = millis();
    if (now - last_ms < 1000U) return;
    heap_free  = ESP.getFreeHeap();
#if defined(ARDUINO_ARCH_ESP32)
    psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    psram_free = 0U;
#endif
    last_ms = now;
  }
};

HeapCache g_heap_cache;

void updateRuntimePerfCounters(AppRuntimeStatus* status, uint32_t tick_us) {
  if (status == nullptr) return;
  if (tick_us > status->max_tick_us) {
    status->max_tick_us = tick_us;
  }
  status->avg_tick_us = (status->avg_tick_us == 0U)
                            ? tick_us
                            : ((status->avg_tick_us * 7U) + tick_us) / 8U;
  g_heap_cache.refresh();
  status->heap_free  = g_heap_cache.heap_free;
  status->psram_free = g_heap_cache.psram_free;
}

// ============================================================================
// BasicAppModule — default behaviour for apps without a dedicated module.
// ID-based dispatch maps app IDs to hardware actions at start/end.
// ============================================================================

class BasicAppModule : public IAppModule {
 public:
  bool begin(const AppContext& ctx) override {
    ctx_    = ctx;
    status_ = {};
    if (ctx.descriptor != nullptr) {
      core::copyText(status_.id, sizeof(status_.id), ctx.descriptor->id);
    }
    status_.state         = AppRuntimeState::kStarting;
    status_.started_at_ms = millis();

    if (ctx.descriptor == nullptr) {
      core::copyText(status_.last_error, sizeof(status_.last_error), "missing_descriptor");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }

    const char* id = ctx.descriptor->id;
    bool ok = true;

    if (core::equalsIgnoreCase(id, "camera_video") ||
        core::equalsIgnoreCase(id, "qr_scanner")) {
      ok = (ctx.camera != nullptr) && ctx.camera->start();
      if (!ok) core::copyText(status_.last_error, sizeof(status_.last_error), "camera_start_failed");

    } else if (core::equalsIgnoreCase(id, "dictaphone")) {
      ok = (ctx.media != nullptr) && ctx.media->startRecording(30U, nullptr);
      if (!ok) core::copyText(status_.last_error, sizeof(status_.last_error), "record_start_failed");

    } else if (core::equalsIgnoreCase(id, "flashlight")) {
      ok = (ctx.hardware != nullptr) && ctx.hardware->setManualLed(255U, 255U, 255U, 120U, false);
      if (!ok) core::copyText(status_.last_error, sizeof(status_.last_error), "flashlight_start_failed");

    } else if (core::equalsIgnoreCase(id, "audio_player")    ||
               core::equalsIgnoreCase(id, "audiobook_player") ||
               core::equalsIgnoreCase(id, "kids_webradio")    ||
               core::equalsIgnoreCase(id, "kids_podcast")     ||
               core::equalsIgnoreCase(id, "kids_music")) {
      ok = (ctx.audio != nullptr) && ctx.audio->play("/apps/audio_player/audio/default.mp3");
      if (!ok) core::copyText(status_.last_error, sizeof(status_.last_error), "audio_start_failed");
    }

    status_.state = ok ? AppRuntimeState::kRunning : AppRuntimeState::kFailed;
    if (ok) core::copyText(status_.last_event, sizeof(status_.last_event), "begin");
    return ok;
  }

  void tick(uint32_t now_ms) override {
    status_.last_tick_ms = now_ms;
    status_.tick_count  += 1U;
  }

  void handleAction(const AppAction& action) override {
    core::copyText(status_.last_event, sizeof(status_.last_event), action.name);
    if (status_.state != AppRuntimeState::kRunning) return;

    if (core::equalsIgnoreCase(action.name, "play")) {
      if (ctx_.media != nullptr && action.payload[0] != '\0') {
        if (!ctx_.media->play(action.payload, ctx_.audio)) {
          core::copyText(status_.last_error, sizeof(status_.last_error), "play_failed");
        }
      }
    } else if (core::equalsIgnoreCase(action.name, "stop")) {
      if (ctx_.media != nullptr) {
        ctx_.media->stop(ctx_.audio);
      } else if (ctx_.audio != nullptr) {
        ctx_.audio->stop();
      }
    } else if (core::equalsIgnoreCase(action.name, "record_start")) {
      uint16_t seconds = static_cast<uint16_t>(std::strtoul(action.payload, nullptr, 10));
      if (seconds == 0U) seconds = 30U;
      if (ctx_.media != nullptr && !ctx_.media->startRecording(seconds, nullptr)) {
        core::copyText(status_.last_error, sizeof(status_.last_error), "record_start_failed");
      }
    } else if (core::equalsIgnoreCase(action.name, "record_stop")) {
      if (ctx_.media != nullptr) ctx_.media->stopRecording();
    } else if (core::equalsIgnoreCase(action.name, "snapshot")) {
      if (ctx_.camera != nullptr) {
        String out_path;
        if (!ctx_.camera->snapshotToFile(nullptr, &out_path)) {
          core::copyText(status_.last_error, sizeof(status_.last_error), "snapshot_failed");
        }
      }
    } else if (core::equalsIgnoreCase(action.name, "light_on")) {
      if (ctx_.hardware != nullptr &&
          !ctx_.hardware->setManualLed(255U, 255U, 255U, 120U, false)) {
        core::copyText(status_.last_error, sizeof(status_.last_error), "light_on_failed");
      }
    } else if (core::equalsIgnoreCase(action.name, "light_off")) {
      if (ctx_.hardware != nullptr) ctx_.hardware->clearManualLed();
    }
  }

  void end() override {
    if (ctx_.descriptor == nullptr) {
      status_.state = AppRuntimeState::kIdle;
      return;
    }
    const char* id = ctx_.descriptor->id;

    if (core::equalsIgnoreCase(id, "camera_video") ||
        core::equalsIgnoreCase(id, "qr_scanner")) {
      if (ctx_.camera != nullptr) ctx_.camera->stop();

    } else if (core::equalsIgnoreCase(id, "dictaphone")) {
      if (ctx_.media != nullptr) ctx_.media->stopRecording();

    } else if (core::equalsIgnoreCase(id, "flashlight")) {
      if (ctx_.hardware != nullptr) ctx_.hardware->clearManualLed();

    } else if (core::equalsIgnoreCase(id, "audio_player")    ||
               core::equalsIgnoreCase(id, "audiobook_player") ||
               core::equalsIgnoreCase(id, "kids_webradio")    ||
               core::equalsIgnoreCase(id, "kids_podcast")     ||
               core::equalsIgnoreCase(id, "kids_music")) {
      if (ctx_.audio != nullptr) ctx_.audio->stop();
    }

    status_.state = AppRuntimeState::kIdle;
    core::copyText(status_.last_event, sizeof(status_.last_event), "end");
  }

  AppRuntimeStatus status() const override { return status_; }

 private:
  AppContext       ctx_    = {};
  AppRuntimeStatus status_ = {};
};

}  // namespace

// ============================================================================
// AppRuntimeManager
// ============================================================================

void AppRuntimeManager::configure(AppRegistry* registry, const AppContext& context) {
  registry_           = registry;
  context_            = context;
  module_.reset();
  current_descriptor_ = nullptr;
  status_             = {};
}

bool AppRuntimeManager::startApp(const AppStartRequest& request, uint32_t now_ms) {
  if (registry_ == nullptr || request.id[0] == '\0') {
    core::copyText(status_.last_error, sizeof(status_.last_error), "app_registry_unavailable");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }

  const AppDescriptor* descriptor = registry_->find(request.id);
  if (descriptor == nullptr) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "app_not_found");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }
  if (!descriptor->enabled) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "app_disabled");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }

  // Stop any currently running app.
  AppStopRequest stop_req = {};
  core::copyText(stop_req.id,     sizeof(stop_req.id),     descriptor->id);
  core::copyText(stop_req.reason, sizeof(stop_req.reason), "switch");
  (void)stopApp(stop_req, now_ms);

  status_ = {};
  core::copyText(status_.id,     sizeof(status_.id),     descriptor->id);
  core::copyText(status_.mode,   sizeof(status_.mode),   request.mode);
  core::copyText(status_.source, sizeof(status_.source), request.source);
  status_.state             = AppRuntimeState::kStarting;
  status_.started_at_ms     = now_ms;
  status_.required_cap_mask = descriptor->required_capabilities;

  if (context_.resource != nullptr) {
    const uint32_t req = descriptor->required_capabilities;
    if (appCapabilityMaskHas(req, CAP_AUDIO_IN)) {
      context_.resource->setProfile(runtime::resource::ResourceProfile::kGfxPlusMic);
    } else if (appCapabilityMaskHas(req, CAP_CAMERA)) {
      context_.resource->setProfile(runtime::resource::ResourceProfile::kGfxPlusCamSnapshot);
      if (context_.hardware != nullptr) {
        context_.hardware->setMicRuntimeEnabled(false);
      }
      if (context_.audio != nullptr) {
        context_.audio->releaseOutputResources();
      }
    }
  }

  status_.missing_cap_mask = evaluateMissingCapabilities(*descriptor);
  if (status_.missing_cap_mask != 0U) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "resource_busy");
    status_.state       = AppRuntimeState::kFailed;
    current_descriptor_ = descriptor;
    return false;
  }

  if (descriptor->supports_offline &&
      descriptor->asset_manifest[0] != '\0' &&
      context_.storage != nullptr &&
      !context_.storage->fileExists(descriptor->asset_manifest)) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "missing_asset");
    status_.state       = AppRuntimeState::kFailed;
    current_descriptor_ = descriptor;
    return false;
  }

  module_ = app::modules::createAppModule(*descriptor);
  if (!module_) {
    module_.reset(new BasicAppModule());
  }

  AppContext run_ctx   = context_;
  run_ctx.descriptor  = descriptor;
  const bool ok       = module_->begin(run_ctx);

  status_ = module_->status();
  core::copyText(status_.mode,   sizeof(status_.mode),   request.mode);
  core::copyText(status_.source, sizeof(status_.source), request.source);
  status_.required_cap_mask = descriptor->required_capabilities;
  status_.missing_cap_mask  = 0U;
  updateRuntimePerfCounters(&status_, 0U);
  current_descriptor_ = descriptor;

  if (ok && context_.ui != nullptr && descriptor->entry_screen[0] != '\0') {
    UiSceneFrame frame       = {};
    frame.screen_scene_id   = descriptor->entry_screen;
    frame.step_id           = descriptor->id;
    frame.audio_playing     = (context_.audio != nullptr) && context_.audio->isPlaying();
    context_.ui->submitSceneFrame(frame);
  }
  return ok;
}

bool AppRuntimeManager::stopApp(const AppStopRequest& request, uint32_t now_ms) {
  (void)now_ms;
  (void)request;
  if (!module_) return true;
  const bool restoring_camera_sidecars =
      current_descriptor_ != nullptr &&
      appCapabilityMaskHas(current_descriptor_->required_capabilities, CAP_CAMERA);

  status_.state = AppRuntimeState::kStopping;
  module_->end();
  status_ = module_->status();
  module_.reset();
  current_descriptor_ = nullptr;
  core::copyText(status_.id, sizeof(status_.id), "");
  status_.missing_cap_mask  = 0U;
  status_.required_cap_mask = 0U;

  if (context_.ui != nullptr) {
    UiSceneFrame frame     = {};
    frame.screen_scene_id  = "SCENE_READY";
    frame.step_id          = "APP_IDLE";
    frame.audio_playing    = (context_.audio != nullptr) && context_.audio->isPlaying();
    context_.ui->submitSceneFrame(frame);
  }
  if (restoring_camera_sidecars && context_.audio != nullptr) {
    context_.audio->restoreOutputResources();
  }
  return true;
}

bool AppRuntimeManager::handleAction(const AppAction& action, uint32_t now_ms) {
  (void)now_ms;
  if (!module_) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "no_app_running");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }
  if (action.id[0] != '\0' && !core::equalsIgnoreCase(action.id, status_.id)) {
    core::copyText(status_.last_error, sizeof(status_.last_error), "app_id_mismatch");
    return false;
  }

  char mode[sizeof(status_.mode)]     = {};
  char source[sizeof(status_.source)] = {};
  core::copyText(mode,   sizeof(mode),   status_.mode);
  core::copyText(source, sizeof(source), status_.source);

  module_->handleAction(action);
  status_ = module_->status();
  core::copyText(status_.mode,   sizeof(status_.mode),   mode);
  core::copyText(status_.source, sizeof(status_.source), source);
  status_.required_cap_mask = (current_descriptor_ != nullptr)
                                  ? current_descriptor_->required_capabilities : 0U;
  status_.missing_cap_mask  = (current_descriptor_ != nullptr)
                                  ? evaluateMissingCapabilities(*current_descriptor_) : 0U;
  updateRuntimePerfCounters(&status_, 0U);
  return true;
}

void AppRuntimeManager::tick(uint32_t now_ms) {
  if (!module_) return;

  char mode[sizeof(status_.mode)]     = {};
  char source[sizeof(status_.source)] = {};
  core::copyText(mode,   sizeof(mode),   status_.mode);
  core::copyText(source, sizeof(source), status_.source);

  const uint32_t start_us  = micros();
  module_->tick(now_ms);
  const uint32_t elapsed_us = micros() - start_us;

  status_ = module_->status();
  core::copyText(status_.mode,   sizeof(status_.mode),   mode);
  core::copyText(status_.source, sizeof(status_.source), source);
  status_.required_cap_mask = (current_descriptor_ != nullptr)
                                  ? current_descriptor_->required_capabilities : 0U;
  status_.missing_cap_mask  = (current_descriptor_ != nullptr)
                                  ? evaluateMissingCapabilities(*current_descriptor_) : 0U;
  updateRuntimePerfCounters(&status_, elapsed_us);
}

AppRuntimeStatus AppRuntimeManager::current() const {
  return status_;
}

const AppDescriptor* AppRuntimeManager::currentDescriptor() const {
  return current_descriptor_;
}

uint32_t AppRuntimeManager::evaluateMissingCapabilities(const AppDescriptor& descriptor) const {
  uint32_t missing = 0U;
  const uint32_t req = descriptor.required_capabilities;
  runtime::resource::ResourceCoordinator* res = context_.resource;

  auto lacking = [&](runtime::resource::ResourceCapability cap) -> bool {
    return res != nullptr && !res->allowsCapability(cap);
  };

  if (appCapabilityMaskHas(req, CAP_AUDIO_OUT)) {
    if (context_.audio == nullptr || lacking(runtime::resource::ResourceCapability::kAudioOut))
      missing |= CAP_AUDIO_OUT;
  }
  if (appCapabilityMaskHas(req, CAP_AUDIO_IN)) {
    const bool has = (context_.media != nullptr) ||
                     (context_.hardware != nullptr && context_.hardware->snapshotRef().mic_ready);
    if (!has || lacking(runtime::resource::ResourceCapability::kAudioIn))
      missing |= CAP_AUDIO_IN;
  }
  if (appCapabilityMaskHas(req, CAP_CAMERA)) {
    const bool has = (context_.camera != nullptr && context_.camera->snapshot().supported);
    if (!has || lacking(runtime::resource::ResourceCapability::kCamera))
      missing |= CAP_CAMERA;
  }
  if (appCapabilityMaskHas(req, CAP_LED)) {
    const bool has = (context_.hardware != nullptr && context_.hardware->snapshotRef().ws2812_ready);
    if (!has || lacking(runtime::resource::ResourceCapability::kLed))
      missing |= CAP_LED;
  }
  if (appCapabilityMaskHas(req, CAP_WIFI)) {
    if (context_.network == nullptr || lacking(runtime::resource::ResourceCapability::kWifi))
      missing |= CAP_WIFI;
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_SD)) {
    const bool has = (context_.storage != nullptr && context_.storage->hasSdCard());
    if (!has || lacking(runtime::resource::ResourceCapability::kStorageSd))
      missing |= CAP_STORAGE_SD;
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_FS)) {
    if (context_.storage == nullptr || lacking(runtime::resource::ResourceCapability::kStorageFs))
      missing |= CAP_STORAGE_FS;
  }
  if (appCapabilityMaskHas(req, CAP_GPU_UI)) {
    if (context_.ui == nullptr || lacking(runtime::resource::ResourceCapability::kGpuUi))
      missing |= CAP_GPU_UI;
  }
  return missing;
}
