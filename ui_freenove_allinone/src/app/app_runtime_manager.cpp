// app_runtime_manager.cpp - app runtime control and lightweight module behavior.
#include "app/app_runtime_manager.h"

#include <cstdlib>
#include <cstring>

#include "app/modules/app_modules.h"
#include "audio_manager.h"
#include "camera_manager.h"
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
    const char lower_a = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
    const char lower_b = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
    if (lower_a != lower_b) {
      return false;
    }
  }
}

class BasicAppModule : public IAppModule {
 public:
  bool begin(const AppContext& context) override {
    context_ = context;
    status_ = {};
    if (context.descriptor != nullptr) {
      copyText(status_.id, sizeof(status_.id), context.descriptor->id);
    }
    status_.state = AppRuntimeState::kStarting;
    status_.started_at_ms = millis();

    if (context.descriptor == nullptr) {
      copyText(status_.last_error, sizeof(status_.last_error), "missing_descriptor");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }

    const char* id = context.descriptor->id;
    bool ok = true;

    if (equalsIgnoreCase(id, "camera_video") || equalsIgnoreCase(id, "qr_scanner")) {
      ok = (context.camera != nullptr) && context.camera->start();
      if (!ok) {
        copyText(status_.last_error, sizeof(status_.last_error), "camera_start_failed");
      }
    } else if (equalsIgnoreCase(id, "dictaphone")) {
      ok = (context.media != nullptr) && context.media->startRecording(30U, nullptr);
      if (!ok) {
        copyText(status_.last_error, sizeof(status_.last_error), "record_start_failed");
      }
    } else if (equalsIgnoreCase(id, "flashlight")) {
      ok = (context.hardware != nullptr) && context.hardware->setManualLed(255U, 255U, 255U, 120U, false);
      if (!ok) {
        copyText(status_.last_error, sizeof(status_.last_error), "flashlight_start_failed");
      }
    } else if (equalsIgnoreCase(id, "audio_player") ||
               equalsIgnoreCase(id, "audiobook_player") ||
               equalsIgnoreCase(id, "kids_webradio") ||
               equalsIgnoreCase(id, "kids_podcast") ||
               equalsIgnoreCase(id, "kids_music")) {
      ok = (context.audio != nullptr) && context.audio->play("/music/boot_radio.mp3");
      if (!ok) {
        copyText(status_.last_error, sizeof(status_.last_error), "audio_start_failed");
      }
    }

    if (!ok) {
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    status_.state = AppRuntimeState::kRunning;
    copyText(status_.last_event, sizeof(status_.last_event), "begin");
    return true;
  }

  void tick(uint32_t now_ms) override {
    status_.last_tick_ms = now_ms;
    status_.tick_count += 1U;
  }

  void handleAction(const AppAction& action) override {
    copyText(status_.last_event, sizeof(status_.last_event), action.name);
    if (status_.state != AppRuntimeState::kRunning) {
      return;
    }
    if (equalsIgnoreCase(action.name, "play")) {
      if (context_.media != nullptr && action.payload[0] != '\0') {
        if (!context_.media->play(action.payload, context_.audio)) {
          copyText(status_.last_error, sizeof(status_.last_error), "play_failed");
        }
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "stop")) {
      if (context_.media != nullptr) {
        context_.media->stop(context_.audio);
      } else if (context_.audio != nullptr) {
        context_.audio->stop();
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "record_start")) {
      uint16_t seconds = static_cast<uint16_t>(std::strtoul(action.payload, nullptr, 10));
      if (seconds == 0U) {
        seconds = 30U;
      }
      if (context_.media != nullptr && !context_.media->startRecording(seconds, nullptr)) {
        copyText(status_.last_error, sizeof(status_.last_error), "record_start_failed");
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "record_stop")) {
      if (context_.media != nullptr) {
        context_.media->stopRecording();
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "snapshot")) {
      if (context_.camera != nullptr) {
        String out_path;
        if (!context_.camera->snapshotToFile(nullptr, &out_path)) {
          copyText(status_.last_error, sizeof(status_.last_error), "snapshot_failed");
        }
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "light_on")) {
      if (context_.hardware != nullptr &&
          !context_.hardware->setManualLed(255U, 255U, 255U, 120U, false)) {
        copyText(status_.last_error, sizeof(status_.last_error), "light_on_failed");
      }
      return;
    }
    if (equalsIgnoreCase(action.name, "light_off")) {
      if (context_.hardware != nullptr) {
        context_.hardware->clearManualLed();
      }
      return;
    }
  }

  void end() override {
    if (context_.descriptor == nullptr) {
      status_.state = AppRuntimeState::kIdle;
      return;
    }
    const char* id = context_.descriptor->id;
    if (equalsIgnoreCase(id, "camera_video") || equalsIgnoreCase(id, "qr_scanner")) {
      if (context_.camera != nullptr) {
        context_.camera->stop();
      }
    } else if (equalsIgnoreCase(id, "dictaphone")) {
      if (context_.media != nullptr) {
        context_.media->stopRecording();
      }
    } else if (equalsIgnoreCase(id, "flashlight")) {
      if (context_.hardware != nullptr) {
        context_.hardware->clearManualLed();
      }
    } else if (equalsIgnoreCase(id, "audio_player") ||
               equalsIgnoreCase(id, "audiobook_player") ||
               equalsIgnoreCase(id, "kids_webradio") ||
               equalsIgnoreCase(id, "kids_podcast") ||
               equalsIgnoreCase(id, "kids_music")) {
      if (context_.audio != nullptr) {
        context_.audio->stop();
      }
    }
    status_.state = AppRuntimeState::kIdle;
    copyText(status_.last_event, sizeof(status_.last_event), "end");
  }

  AppRuntimeStatus status() const override {
    return status_;
  }

 private:
  static void copyText(char* out, size_t out_size, const char* text) {
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

  AppContext context_ = {};
  AppRuntimeStatus status_ = {};
};

void updateRuntimePerfCounters(AppRuntimeStatus* status, uint32_t tick_us) {
  if (status == nullptr) {
    return;
  }
  if (tick_us > status->max_tick_us) {
    status->max_tick_us = tick_us;
  }
  if (status->avg_tick_us == 0U) {
    status->avg_tick_us = tick_us;
  } else {
    status->avg_tick_us = ((status->avg_tick_us * 7U) + tick_us) / 8U;
  }
  status->heap_free = ESP.getFreeHeap();
#if defined(ARDUINO_ARCH_ESP32)
  status->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
  status->psram_free = 0U;
#endif
}

}  // namespace

void AppRuntimeManager::configure(AppRegistry* registry, const AppContext& context) {
  registry_ = registry;
  context_ = context;
  module_.reset();
  current_descriptor_ = nullptr;
  status_ = {};
}

bool AppRuntimeManager::startApp(const AppStartRequest& request, uint32_t now_ms) {
  if (registry_ == nullptr || request.id[0] == '\0') {
    copyText(status_.last_error, sizeof(status_.last_error), "app_registry_unavailable");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }
  const AppDescriptor* descriptor = registry_->find(request.id);
  if (descriptor == nullptr) {
    copyText(status_.last_error, sizeof(status_.last_error), "app_not_found");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }
  if (!descriptor->enabled) {
    copyText(status_.last_error, sizeof(status_.last_error), "app_disabled");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }

  AppStopRequest stop_request = {};
  copyText(stop_request.id, sizeof(stop_request.id), descriptor->id);
  copyText(stop_request.reason, sizeof(stop_request.reason), "switch");
  (void)stopApp(stop_request, now_ms);

  status_ = {};
  copyText(status_.id, sizeof(status_.id), descriptor->id);
  copyText(status_.mode, sizeof(status_.mode), request.mode);
  copyText(status_.source, sizeof(status_.source), request.source);
  status_.state = AppRuntimeState::kStarting;
  status_.started_at_ms = now_ms;
  status_.required_cap_mask = descriptor->required_capabilities;
  if (context_.resource != nullptr) {
    const uint32_t req = descriptor->required_capabilities;
    if (appCapabilityMaskHas(req, CAP_AUDIO_IN)) {
      context_.resource->setProfile(runtime::resource::ResourceProfile::kGfxPlusMic);
    } else if (appCapabilityMaskHas(req, CAP_CAMERA)) {
      context_.resource->setProfile(runtime::resource::ResourceProfile::kGfxPlusCamSnapshot);
    }
  }
  status_.missing_cap_mask = evaluateMissingCapabilities(*descriptor);
  if (status_.missing_cap_mask != 0U) {
    copyText(status_.last_error, sizeof(status_.last_error), "resource_busy");
    status_.state = AppRuntimeState::kFailed;
    current_descriptor_ = descriptor;
    return false;
  }
  if (descriptor->supports_offline &&
      descriptor->asset_manifest[0] != '\0' &&
      context_.storage != nullptr &&
      !context_.storage->fileExists(descriptor->asset_manifest)) {
    copyText(status_.last_error, sizeof(status_.last_error), "missing_asset");
    status_.state = AppRuntimeState::kFailed;
    current_descriptor_ = descriptor;
    return false;
  }

  module_ = app::modules::createAppModule(*descriptor);
  if (!module_) {
    module_.reset(new BasicAppModule());
  }
  AppContext run_context = context_;
  run_context.descriptor = descriptor;
  const bool ok = module_->begin(run_context);
  status_ = module_->status();
  copyText(status_.mode, sizeof(status_.mode), request.mode);
  copyText(status_.source, sizeof(status_.source), request.source);
  status_.required_cap_mask = descriptor->required_capabilities;
  status_.missing_cap_mask = 0U;
  updateRuntimePerfCounters(&status_, 0U);
  current_descriptor_ = descriptor;
  if (ok && context_.ui != nullptr && descriptor->entry_screen[0] != '\0') {
    UiSceneFrame frame = {};
    frame.screen_scene_id = descriptor->entry_screen;
    frame.step_id = descriptor->id;
    frame.audio_playing = (context_.audio != nullptr) ? context_.audio->isPlaying() : false;
    context_.ui->submitSceneFrame(frame);
  }
  return ok;
}

bool AppRuntimeManager::stopApp(const AppStopRequest& request, uint32_t now_ms) {
  (void)now_ms;
  (void)request;
  if (!module_) {
    return true;
  }
  status_.state = AppRuntimeState::kStopping;
  module_->end();
  status_ = module_->status();
  module_.reset();
  current_descriptor_ = nullptr;
  copyText(status_.id, sizeof(status_.id), "");
  status_.missing_cap_mask = 0U;
  status_.required_cap_mask = 0U;
  if (context_.ui != nullptr) {
    UiSceneFrame frame = {};
    frame.screen_scene_id = "SCENE_READY";
    frame.step_id = "APP_IDLE";
    frame.audio_playing = (context_.audio != nullptr) ? context_.audio->isPlaying() : false;
    context_.ui->submitSceneFrame(frame);
  }
  return true;
}

bool AppRuntimeManager::handleAction(const AppAction& action, uint32_t now_ms) {
  (void)now_ms;
  if (!module_) {
    copyText(status_.last_error, sizeof(status_.last_error), "no_app_running");
    status_.state = AppRuntimeState::kFailed;
    return false;
  }
  if (action.id[0] != '\0' && !equalsIgnoreCase(action.id, status_.id)) {
    copyText(status_.last_error, sizeof(status_.last_error), "app_id_mismatch");
    return false;
  }
  char mode[sizeof(status_.mode)] = {0};
  char source[sizeof(status_.source)] = {0};
  copyText(mode, sizeof(mode), status_.mode);
  copyText(source, sizeof(source), status_.source);
  module_->handleAction(action);
  status_ = module_->status();
  copyText(status_.mode, sizeof(status_.mode), mode);
  copyText(status_.source, sizeof(status_.source), source);
  status_.required_cap_mask = (current_descriptor_ != nullptr) ? current_descriptor_->required_capabilities : 0U;
  status_.missing_cap_mask = (current_descriptor_ != nullptr) ? evaluateMissingCapabilities(*current_descriptor_) : 0U;
  updateRuntimePerfCounters(&status_, 0U);
  return true;
}

void AppRuntimeManager::tick(uint32_t now_ms) {
  if (!module_) {
    return;
  }
  char mode[sizeof(status_.mode)] = {0};
  char source[sizeof(status_.source)] = {0};
  copyText(mode, sizeof(mode), status_.mode);
  copyText(source, sizeof(source), status_.source);
  const uint32_t start_us = micros();
  module_->tick(now_ms);
  const uint32_t elapsed_us = micros() - start_us;
  status_ = module_->status();
  copyText(status_.mode, sizeof(status_.mode), mode);
  copyText(status_.source, sizeof(status_.source), source);
  status_.required_cap_mask = (current_descriptor_ != nullptr) ? current_descriptor_->required_capabilities : 0U;
  status_.missing_cap_mask = (current_descriptor_ != nullptr) ? evaluateMissingCapabilities(*current_descriptor_) : 0U;
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
  runtime::resource::ResourceCoordinator* resource = context_.resource;

  if (appCapabilityMaskHas(req, CAP_AUDIO_OUT) && context_.audio == nullptr) {
    missing |= CAP_AUDIO_OUT;
  }
  if (appCapabilityMaskHas(req, CAP_AUDIO_OUT) && resource != nullptr &&
      !resource->allowsCapability(runtime::resource::ResourceCapability::kAudioOut)) {
    missing |= CAP_AUDIO_OUT;
  }
  if (appCapabilityMaskHas(req, CAP_AUDIO_IN)) {
    const bool has_audio_in = (context_.media != nullptr) ||
                              (context_.hardware != nullptr && context_.hardware->snapshotRef().mic_ready);
    if (!has_audio_in) {
      missing |= CAP_AUDIO_IN;
    }
    if (resource != nullptr &&
        !resource->allowsCapability(runtime::resource::ResourceCapability::kAudioIn)) {
      missing |= CAP_AUDIO_IN;
    }
  }
  if (appCapabilityMaskHas(req, CAP_CAMERA)) {
    const bool has_camera = (context_.camera != nullptr && context_.camera->snapshot().supported);
    const bool allowed_by_profile =
        (resource == nullptr) || resource->allowsCapability(runtime::resource::ResourceCapability::kCamera);
    if (!has_camera || !allowed_by_profile) {
      missing |= CAP_CAMERA;
    }
  }
  if (appCapabilityMaskHas(req, CAP_LED)) {
    const bool has_led = (context_.hardware != nullptr && context_.hardware->snapshotRef().ws2812_ready);
    if (!has_led) {
      missing |= CAP_LED;
    }
    if (resource != nullptr &&
        !resource->allowsCapability(runtime::resource::ResourceCapability::kLed)) {
      missing |= CAP_LED;
    }
  }
  if (appCapabilityMaskHas(req, CAP_WIFI)) {
    if (context_.network == nullptr) {
      missing |= CAP_WIFI;
    }
    if (resource != nullptr &&
        !resource->allowsCapability(runtime::resource::ResourceCapability::kWifi)) {
      missing |= CAP_WIFI;
    }
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_SD) &&
      (context_.storage == nullptr || !context_.storage->hasSdCard())) {
    missing |= CAP_STORAGE_SD;
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_SD) && resource != nullptr &&
      !resource->allowsCapability(runtime::resource::ResourceCapability::kStorageSd)) {
    missing |= CAP_STORAGE_SD;
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_FS) && context_.storage == nullptr) {
    missing |= CAP_STORAGE_FS;
  }
  if (appCapabilityMaskHas(req, CAP_STORAGE_FS) && resource != nullptr &&
      !resource->allowsCapability(runtime::resource::ResourceCapability::kStorageFs)) {
    missing |= CAP_STORAGE_FS;
  }
  if (appCapabilityMaskHas(req, CAP_GPU_UI) && context_.ui == nullptr) {
    missing |= CAP_GPU_UI;
  }
  if (appCapabilityMaskHas(req, CAP_GPU_UI) && resource != nullptr &&
      !resource->allowsCapability(runtime::resource::ResourceCapability::kGpuUi)) {
    missing |= CAP_GPU_UI;
  }

  return missing;
}

void AppRuntimeManager::copyText(char* out, size_t out_size, const char* text) {
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
