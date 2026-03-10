// app_modules.cpp - MVP core app modules.
#include "app/modules/app_modules.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "core/str_utils.h"

#include "audio_manager.h"
#include "camera_manager.h"
#include "hardware_manager.h"
#include "media_manager.h"
#include "network_manager.h"
#include "storage_manager.h"

#if __has_include(<tinyexpr.h>)
#include <tinyexpr.h>
#define ZACUS_USE_TINYEXPR 1
#else
#define ZACUS_USE_TINYEXPR 0
#endif

namespace app::modules {
namespace {

constexpr const char* kSharedBundledAudioTrack = "/apps/audio_player/audio/default.mp3";

const char* defaultBundledAudioForApp(const char* app_id) {
  if (app_id == nullptr || app_id[0] == '\0') {
    return kSharedBundledAudioTrack;
  }
  if (core::equalsIgnoreCase(app_id, "audio_player")) {
    return "/apps/audio_player/audio/default.mp3";
  }
  if (core::equalsIgnoreCase(app_id, "audiobook_player")) {
    return "/apps/audiobook_player/audio/default.mp3";
  }
  if (core::equalsIgnoreCase(app_id, "kids_music")) {
    return "/apps/kids_music/audio/default.mp3";
  }
  if (core::equalsIgnoreCase(app_id, "kids_webradio")) {
    return "/apps/kids_webradio/audio/default.mp3";
  }
  if (core::equalsIgnoreCase(app_id, "kids_podcast")) {
    return "/apps/kids_podcast/audio/default.mp3";
  }
  return kSharedBundledAudioTrack;
}

bool ensureDir(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (LittleFS.exists(path)) {
    return true;
  }
  return LittleFS.mkdir(path);
}

bool writeTextFile(const char* path, const String& payload) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  const size_t written = file.print(payload);
  file.close();
  return written == payload.length();
}

String readTextFile(const char* path) {
  if (path == nullptr || path[0] == '\0' || !LittleFS.exists(path)) {
    return String();
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return String();
  }
  String payload;
  payload.reserve(static_cast<unsigned int>(file.size() + 1U));
  while (file.available()) {
    payload += static_cast<char>(file.read());
  }
  file.close();
  return payload;
}

uint32_t parseUint(const char* text, uint32_t fallback = 0U) {
  if (text == nullptr || text[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (end == text) {
    return fallback;
  }
  return static_cast<uint32_t>(value);
}

bool startsWithIgnoreCase(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  for (size_t i = 0U;; ++i) {
    const char a = text[i];
    const char b = prefix[i];
    if (b == '\0') {
      return true;
    }
    if (a == '\0') {
      return false;
    }
    const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
    const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
    if (la != lb) {
      return false;
    }
  }
}

bool endsWithIgnoreCase(const char* text, const char* suffix) {
  if (text == nullptr || suffix == nullptr) {
    return false;
  }
  const size_t text_len = std::strlen(text);
  const size_t suffix_len = std::strlen(suffix);
  if (suffix_len == 0U || suffix_len > text_len) {
    return false;
  }
  const char* tail = text + (text_len - suffix_len);
  return core::equalsIgnoreCase(tail, suffix);
}

bool parseJsonPayload(const AppAction& action, DynamicJsonDocument* out) {
  if (out == nullptr) {
    return false;
  }
  const bool looks_json = (action.content_type[0] != '\0' && core::equalsIgnoreCase(action.content_type, "application/json")) ||
                          action.payload[0] == '{' || action.payload[0] == '[';
  if (!looks_json || action.payload[0] == '\0') {
    return false;
  }
  out->clear();
  return deserializeJson(*out, action.payload) == DeserializationError::Ok;
}

class ModuleBase : public IAppModule {
 public:
  bool begin(const AppContext& context) override {
    context_ = context;
    status_ = {};
    if (context.descriptor != nullptr) {
      core::copyText(status_.id, sizeof(status_.id), context.descriptor->id);
    }
    status_.state = AppRuntimeState::kRunning;
    status_.started_at_ms = millis();
    core::copyText(status_.last_event, sizeof(status_.last_event), "begin");
    return context.descriptor != nullptr;
  }

  void tick(uint32_t now_ms) override {
    status_.last_tick_ms = now_ms;
    status_.tick_count += 1U;
  }

  void handleAction(const AppAction& action) override {
    core::copyText(status_.last_event, sizeof(status_.last_event), action.name);
  }

  void end() override {
    status_.state = AppRuntimeState::kIdle;
    core::copyText(status_.last_event, sizeof(status_.last_event), "end");
  }

  AppRuntimeStatus status() const override {
    return status_;
  }

 protected:
  void setError(const char* error) {
    core::copyText(status_.last_error, sizeof(status_.last_error), error);
  }

  AppContext context_ = {};
  AppRuntimeStatus status_ = {};
};

class AudioPlayerModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.audio == nullptr || context.media == nullptr) {
      setError("audio_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "play")) {
      const char* path = (action.payload[0] != '\0')
                             ? action.payload
                             : defaultBundledAudioForApp((context_.descriptor != nullptr) ? context_.descriptor->id : nullptr);
      if (context_.storage != nullptr && !context_.storage->fileExists(path)) {
        setError("missing_asset");
        return;
      }
      if (!context_.media->play(path, context_.audio)) {
        setError("missing_asset");
      } else {
        paused_url_.remove(0);
        paused_track_ = path;
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "play_url")) {
      if (action.payload[0] == '\0') {
        setError("missing_asset");
        return;
      }
      if (context_.network != nullptr) {
        const NetworkManager::Snapshot net = context_.network->snapshot();
        if (!net.sta_connected) {
          if (!playOfflineFallback()) {
            setError("network_unavailable");
          }
          return;
        }
      }
      if (!context_.audio->play(action.payload)) {
        if (!playOfflineFallback()) {
          setError("network_unavailable");
        }
      } else {
        paused_url_ = action.payload;
        paused_track_.remove(0);
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "pause")) {
      if (!paused_url_.isEmpty()) {
        context_.audio->stop();
      } else {
        paused_track_ = context_.audio->currentTrack();
        context_.audio->stop();
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "resume")) {
      bool ok = false;
      if (!paused_url_.isEmpty()) {
        ok = context_.audio->play(paused_url_.c_str());
      } else if (!paused_track_.isEmpty()) {
        ok = context_.media->play(paused_track_.c_str(), context_.audio);
      }
      if (!ok) {
        setError("resume_failed");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "stop")) {
      context_.media->stop(context_.audio);
      return;
    }
    if (core::equalsIgnoreCase(action.name, "set_volume")) {
      const uint32_t volume = parseUint(action.payload, context_.audio->volume());
      context_.audio->setVolume(static_cast<uint8_t>(volume > 100U ? 100U : volume));
      return;
    }
    if (core::equalsIgnoreCase(action.name, "next") || core::equalsIgnoreCase(action.name, "prev")) {
      core::copyText(status_.last_event, sizeof(status_.last_event), "playlist_not_configured");
      return;
    }
  }

 private:
  bool playOfflineFallback() {
    if (context_.media == nullptr || context_.audio == nullptr || context_.storage == nullptr) {
      return false;
    }
    const char* app_id = (context_.descriptor != nullptr) ? context_.descriptor->id : "";
    String candidates[3];
    candidates[0] = String("/apps/") + app_id + "/audio/offline.mp3";
    candidates[1] = String("/apps/") + app_id + "/audio/default.mp3";
    candidates[2] = kSharedBundledAudioTrack;
    for (const String& candidate : candidates) {
      if (!context_.storage->fileExists(candidate.c_str())) {
        continue;
      }
      if (context_.media->play(candidate.c_str(), context_.audio)) {
        core::copyText(status_.last_event, sizeof(status_.last_event), "offline_fallback");
        setError("");
        return true;
      }
    }
    return false;
  }

  String paused_track_;
  String paused_url_;
};

class AudiobookModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.audio == nullptr || context.media == nullptr) {
      setError("audio_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    loadProgress();
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "open_book")) {
      current_book_ = action.payload;
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "play")) {
      const char* target = current_book_.isEmpty() ? action.payload : current_book_.c_str();
      if (target == nullptr || target[0] == '\0') {
        setError("missing_asset");
        return;
      }
      if (startsWithIgnoreCase(target, "http://") || startsWithIgnoreCase(target, "https://")) {
        if (context_.network != nullptr) {
          const NetworkManager::Snapshot net = context_.network->snapshot();
          if (!net.sta_connected) {
            setError("network_unavailable");
            return;
          }
        }
        if (!context_.audio->play(target)) {
          setError("network_unavailable");
        }
        return;
      }
      if (context_.storage != nullptr && !context_.storage->fileExists(target)) {
        setError("missing_asset");
        return;
      }
      if (!context_.media->play(target, context_.audio)) {
        setError("missing_asset");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "pause")) {
      context_.audio->stop();
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "stop")) {
      context_.media->stop(context_.audio);
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "seek_ms")) {
      position_ms_ = parseUint(action.payload, position_ms_);
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "bookmark_set")) {
      bookmark_ms_ = parseUint(action.payload, position_ms_);
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "bookmark_go")) {
      position_ms_ = bookmark_ms_;
      saveProgress();
      return;
    }
  }

  void end() override {
    saveProgress();
    ModuleBase::end();
  }

 private:
  void loadProgress() {
    const String payload = readTextFile(kProgressPath);
    if (payload.isEmpty()) {
      return;
    }
    DynamicJsonDocument document(512);
    if (deserializeJson(document, payload) != DeserializationError::Ok) {
      return;
    }
    current_book_ = document["book"] | "";
    position_ms_ = document["position_ms"] | 0U;
    bookmark_ms_ = document["bookmark_ms"] | 0U;
  }

  void saveProgress() {
    ensureDir("/apps");
    ensureDir("/apps/audiobook_player");
    DynamicJsonDocument document(512);
    document["book"] = current_book_;
    document["position_ms"] = position_ms_;
    document["bookmark_ms"] = bookmark_ms_;
    document["updated_at_ms"] = millis();
    String payload;
    serializeJson(document, payload);
    if (!writeTextFile(kProgressPath, payload)) {
      setError("progress_save_failed");
    }
  }

  static constexpr const char* kProgressPath = "/apps/audiobook_player/progress.json";
  String current_book_;
  uint32_t position_ms_ = 0U;
  uint32_t bookmark_ms_ = 0U;
};

class CameraVideoModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.camera == nullptr) {
      setError("camera_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    bool camera_ready = context.camera->startRecorderSession();
    preview_on_ = camera_ready;
    if (!camera_ready) {
      camera_ready = context.camera->start();
      preview_on_ = false;
    }
    if (!camera_ready) {
      setError("camera_start_failed");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    clip_active_ = false;
    clip_frames_.clear();
    setError("");
    core::copyText(status_.last_event,
                   sizeof(status_.last_event),
                   preview_on_ ? "preview_ready" : "snapshot_ready");
    updateStatusEvent();
    return true;
  }

  void tick(uint32_t now_ms) override {
    ModuleBase::tick(now_ms);
    if (!clip_active_) {
      return;
    }
    if (now_ms < next_frame_ms_) {
      return;
    }
    next_frame_ms_ = now_ms + 1000U;
    String out_path;
    char hint[40] = {0};
    std::snprintf(hint, sizeof(hint), "clip_%lu.jpg", static_cast<unsigned long>(now_ms));
    if (context_.camera->snapshotToFile(hint, &out_path)) {
      clip_frames_.push_back(out_path);
    } else {
      setError("clip_frame_failed");
    }
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "status")) {
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "preview_on")) {
      if (!context_.camera->start()) {
        setError("preview_on_failed");
      } else {
        preview_on_ = true;
        setError("");
        updateStatusEvent();
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "preview_off")) {
      context_.camera->stop();
      preview_on_ = false;
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "snapshot")) {
      String out_path;
      if (!context_.camera->snapshotToFile(nullptr, &out_path)) {
        setError("snapshot_failed");
      } else {
        char event[40] = {0};
        std::snprintf(event, sizeof(event), "snap=%u", static_cast<unsigned int>(out_path.length()));
        core::copyText(status_.last_event, sizeof(status_.last_event), event);
        setError("");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "clip_start")) {
      clip_active_ = true;
      clip_frames_.clear();
      clip_started_ms_ = millis();
      next_frame_ms_ = clip_started_ms_;
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "clip_stop")) {
      stopClipAndPersist();
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "list_media")) {
      static constexpr int kMaxListItems = 12;
      String items[kMaxListItems];
      const int count = context_.camera->recorderListPhotos(items, kMaxListItems, true);
      if (count < 0) {
        setError("list_media_failed");
      } else {
        char event[40] = {0};
        std::snprintf(event, sizeof(event), "list=%d", count);
        core::copyText(status_.last_event, sizeof(status_.last_event), event);
        setError("");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "delete_media")) {
      if (action.payload[0] == '\0' || !context_.camera->recorderRemoveFile(action.payload)) {
        setError("delete_media_failed");
      } else {
        setError("");
        core::copyText(status_.last_event, sizeof(status_.last_event), "delete_ok");
      }
      return;
    }
    setError("unsupported_action");
  }

  void end() override {
    stopClipAndPersist();
    if (context_.camera != nullptr) {
      context_.camera->stop();
    }
    ModuleBase::end();
  }

 private:
  void stopClipAndPersist() {
    if (!clip_active_) {
      return;
    }
    clip_active_ = false;
    ensureDir("/apps");
    ensureDir("/apps/camera_video");
    ensureDir("/apps/camera_video/cache");
    DynamicJsonDocument document(4096);
    document["started_at_ms"] = clip_started_ms_;
    document["stopped_at_ms"] = millis();
    JsonArray frames = document["frames"].to<JsonArray>();
    for (const String& frame : clip_frames_) {
      frames.add(frame);
    }
    String payload;
    serializeJson(document, payload);
    char index_path[96] = {0};
    std::snprintf(index_path,
                  sizeof(index_path),
                  "/apps/camera_video/cache/clip_%lu.index.json",
                  static_cast<unsigned long>(clip_started_ms_));
    if (!writeTextFile(index_path, payload)) {
      setError("clip_index_write_failed");
    }
    clip_frames_.clear();
  }

  void updateStatusEvent() {
    char event[40] = {0};
    std::snprintf(event,
                  sizeof(event),
                  "pv=%u clip=%u frames=%u",
                  preview_on_ ? 1U : 0U,
                  clip_active_ ? 1U : 0U,
                  static_cast<unsigned int>(clip_frames_.size()));
    core::copyText(status_.last_event, sizeof(status_.last_event), event);
  }

  bool preview_on_ = false;
  bool clip_active_ = false;
  uint32_t clip_started_ms_ = 0U;
  uint32_t next_frame_ms_ = 0U;
  std::vector<String> clip_frames_ = {};
};

class QrScannerModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.camera == nullptr) {
      setError("camera_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    // QR scene owns camera lifecycle through ESP32QRCodeReader.
    // Ensure CameraManager is not already holding the camera driver.
    context.camera->stop();
    scanning_ = false;
    core::copyText(last_type_, sizeof(last_type_), "none");
    setError("");
    updateStatusEvent();
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "status")) {
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "scan_start")) {
      scanning_ = true;
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "scan_stop")) {
      scanning_ = false;
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "scan_once") || core::equalsIgnoreCase(action.name, "scan_payload")) {
      scanning_ = false;
      classifyPayload(action.payload);
      setError("");
      updateStatusEvent();
      return;
    }
    setError("unsupported_action");
  }

  void end() override {
    ModuleBase::end();
  }

 private:
  void classifyPayload(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') {
      core::copyText(last_type_, sizeof(last_type_), "unknown");
      return;
    }
    if (std::strncmp(payload, "http://", 7U) == 0 || std::strncmp(payload, "https://", 8U) == 0) {
      core::copyText(last_type_, sizeof(last_type_), "url");
      return;
    }
    if (std::strncmp(payload, "app:", 4U) == 0 || std::strncmp(payload, "zacus:", 6U) == 0) {
      core::copyText(last_type_, sizeof(last_type_), "app");
      return;
    }
    core::copyText(last_type_, sizeof(last_type_), "text");
  }

  void updateStatusEvent() {
    char event[40] = {0};
    std::snprintf(event,
                  sizeof(event),
                  "scan=%u type=%s",
                  scanning_ ? 1U : 0U,
                  last_type_);
    core::copyText(status_.last_event, sizeof(status_.last_event), event);
  }

  bool scanning_ = false;
  char last_type_[12] = "none";
};

class DictaphoneModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.media == nullptr || context.audio == nullptr) {
      setError("media_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    setError("");
    updateStatusEvent();
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "status")) {
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "record_start")) {
      const uint16_t sec = static_cast<uint16_t>(parseUint(action.payload, 30U));
      if (!context_.media->startRecording(sec, nullptr)) {
        setError("record_start_failed");
      } else {
        setError("");
        core::copyText(status_.last_event, sizeof(status_.last_event), "recording=1");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "record_stop")) {
      if (!context_.media->stopRecording()) {
        setError("record_stop_failed");
      } else {
        setError("");
        core::copyText(status_.last_event, sizeof(status_.last_event), "recording=0");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "play_file")) {
      char path[120] = {0};
      if (!normalizeRecordPath(action.payload, path, sizeof(path)) ||
          !context_.media->play(path, context_.audio)) {
        setError("play_file_failed");
      } else {
        setError("");
        core::copyText(status_.last_event, sizeof(status_.last_event), "play_ok");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "delete_file")) {
      char path[120] = {0};
      if (!normalizeRecordPath(action.payload, path, sizeof(path)) || !LittleFS.remove(path)) {
        setError("delete_file_failed");
      } else {
        setError("");
        core::copyText(status_.last_event, sizeof(status_.last_event), "delete_ok");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "list_records")) {
      String list_json;
      if (!context_.media->listFiles("records", &list_json)) {
        setError("list_records_failed");
      } else {
        DynamicJsonDocument list_doc(2048);
        size_t count = 0U;
        if (deserializeJson(list_doc, list_json) == DeserializationError::Ok && list_doc.is<JsonArrayConst>()) {
          count = list_doc.as<JsonArrayConst>().size();
        }
        char event[40] = {0};
        std::snprintf(event, sizeof(event), "records=%u", static_cast<unsigned int>(count));
        core::copyText(status_.last_event, sizeof(status_.last_event), event);
        setError("");
      }
      return;
    }
    setError("unsupported_action");
  }

  void end() override {
    if (context_.media != nullptr) {
      context_.media->stopRecording();
    }
    ModuleBase::end();
  }

 private:
  void updateStatusEvent() {
    core::copyText(status_.last_event, sizeof(status_.last_event), "records=ready");
  }

  bool normalizeRecordPath(const char* payload, char* out, size_t out_size) const {
    if (out == nullptr || out_size == 0U || payload == nullptr || payload[0] == '\0') {
      return false;
    }
    if (payload[0] == '/') {
      core::copyText(out, out_size, payload);
      return true;
    }
    char path[120] = {0};
    std::snprintf(path, sizeof(path), "/recorder/%s", payload);
    core::copyText(out, out_size, path);
    return true;
  }
};

class TimerToolsModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    sw_running_ = false;
    sw_started_ms_ = 0U;
    sw_acc_ms_ = 0U;
    sw_elapsed_ms_ = 0U;
    sw_lap_ms_ = 0U;
    cd_running_ = false;
    cd_started_ms_ = 0U;
    cd_duration_ms_ = 0U;
    cd_remaining_ms_ = 0U;
    cd_done_notified_ = false;
    countdown_visual_until_ms_ = 0U;
    setError("");
    core::copyText(status_.last_event, sizeof(status_.last_event), "timer_ready");
    return true;
  }

  void tick(uint32_t now_ms) override {
    ModuleBase::tick(now_ms);
    if (sw_running_) {
      sw_elapsed_ms_ = sw_acc_ms_ + (now_ms - sw_started_ms_);
    }
    if (cd_running_) {
      const uint32_t elapsed = now_ms - cd_started_ms_;
      if (elapsed >= cd_duration_ms_) {
        cd_remaining_ms_ = 0U;
        cd_running_ = false;
        if (!cd_done_notified_) {
          cd_done_notified_ = true;
          notifyCountdownDone(now_ms);
        }
      } else {
        cd_remaining_ms_ = cd_duration_ms_ - elapsed;
      }
    }
    if (countdown_visual_until_ms_ != 0U && static_cast<int32_t>(now_ms - countdown_visual_until_ms_) >= 0) {
      if (context_.hardware != nullptr) {
        context_.hardware->clearManualLed();
      }
      countdown_visual_until_ms_ = 0U;
    }
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    const uint32_t now_ms = millis();
    if (core::equalsIgnoreCase(action.name, "status")) {
      writeStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "sw_start")) {
      if (!sw_running_) {
        sw_running_ = true;
        sw_started_ms_ = now_ms;
      }
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "sw_stop")) {
      if (sw_running_) {
        sw_acc_ms_ += (now_ms - sw_started_ms_);
        sw_running_ = false;
      }
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "sw_lap")) {
      sw_lap_ms_ = sw_running_ ? (sw_acc_ms_ + (now_ms - sw_started_ms_)) : sw_acc_ms_;
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "sw_reset")) {
      sw_running_ = false;
      sw_started_ms_ = 0U;
      sw_acc_ms_ = 0U;
      sw_elapsed_ms_ = 0U;
      sw_lap_ms_ = 0U;
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "cd_set")) {
      DynamicJsonDocument body(192);
      uint32_t seconds = 0U;
      if (parseJsonPayload(action, &body) && body["seconds"].is<uint32_t>()) {
        seconds = body["seconds"].as<uint32_t>();
      } else {
        seconds = parseUint(action.payload, 0U);
      }
      cd_duration_ms_ = seconds * 1000U;
      cd_remaining_ms_ = cd_duration_ms_;
      cd_running_ = false;
      cd_done_notified_ = false;
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "cd_start")) {
      if (cd_duration_ms_ == 0U) {
        cd_duration_ms_ = 60000U;
      }
      cd_started_ms_ = now_ms;
      cd_running_ = true;
      cd_done_notified_ = false;
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "cd_pause")) {
      if (cd_running_) {
        const uint32_t elapsed = now_ms - cd_started_ms_;
        cd_duration_ms_ = (elapsed >= cd_duration_ms_) ? 0U : (cd_duration_ms_ - elapsed);
        cd_remaining_ms_ = cd_duration_ms_;
        cd_running_ = false;
      }
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "cd_reset")) {
      cd_running_ = false;
      cd_duration_ms_ = 0U;
      cd_remaining_ms_ = 0U;
      cd_done_notified_ = false;
      setError("");
      return;
    }
    setError("unsupported_action");
  }

 private:
  void notifyCountdownDone(uint32_t now_ms) {
    core::copyText(status_.last_event, sizeof(status_.last_event), "countdown_done");
    setError("");
    if (context_.storage != nullptr && context_.audio != nullptr && context_.media != nullptr) {
      static constexpr const char* kDoneCandidates[] = {
          "/apps/timer_tools/audio/action.wav",
          "/apps/shared/audio/ui_success.wav",
      };
      for (const char* path : kDoneCandidates) {
        if (path == nullptr || !context_.storage->fileExists(path)) {
          continue;
        }
        if (context_.media->play(path, context_.audio)) {
          break;
        }
      }
    }
    if (context_.hardware != nullptr &&
        context_.hardware->setManualLed(255U, 180U, 32U, 120U, true)) {
      countdown_visual_until_ms_ = now_ms + 1500U;
    }
  }

  void writeStatusEvent() {
    char event[64] = {0};
    std::snprintf(event,
                  sizeof(event),
                  "sw=%lu cd=%lu run=%u",
                  static_cast<unsigned long>(sw_elapsed_ms_),
                  static_cast<unsigned long>(cd_remaining_ms_),
                  cd_running_ ? 1U : 0U);
    core::copyText(status_.last_event, sizeof(status_.last_event), event);
    setError("");
  }

  bool sw_running_ = false;
  uint32_t sw_started_ms_ = 0U;
  uint32_t sw_acc_ms_ = 0U;
  uint32_t sw_elapsed_ms_ = 0U;
  uint32_t sw_lap_ms_ = 0U;

  bool cd_running_ = false;
  uint32_t cd_started_ms_ = 0U;
  uint32_t cd_duration_ms_ = 0U;
  uint32_t cd_remaining_ms_ = 0U;
  bool cd_done_notified_ = false;
  uint32_t countdown_visual_until_ms_ = 0U;
};

class FlashlightModule : public ModuleBase {
 public:
  static constexpr uint8_t kMaxSafeLevel = 128U;

  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.hardware == nullptr) {
      setError("hardware_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    is_on_ = false;
    level_ = 96U;
    setError("");
    core::copyText(status_.last_event, sizeof(status_.last_event), "light_ready");
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "status")) {
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "light_on")) {
      if (!applyLight(true)) {
        setError("light_on_failed");
      } else {
        setError("");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "light_off")) {
      context_.hardware->clearManualLed();
      is_on_ = false;
      setError("");
      updateStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "light_toggle")) {
      if (!applyLight(!is_on_)) {
        setError("light_toggle_failed");
      } else {
        setError("");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "set_level")) {
      DynamicJsonDocument body(128);
      uint32_t parsed = level_;
      if (parseJsonPayload(action, &body) && body["level"].is<uint32_t>()) {
        parsed = body["level"].as<uint32_t>();
      } else {
        parsed = parseUint(action.payload, level_);
      }
      level_ = static_cast<uint8_t>(parsed > kMaxSafeLevel ? kMaxSafeLevel : parsed);
      if (is_on_ && !applyLight(true)) {
        setError("set_level_failed");
        return;
      }
      setError("");
      updateStatusEvent();
      return;
    }
    setError("unsupported_action");
  }

  void end() override {
    if (context_.hardware != nullptr) {
      context_.hardware->clearManualLed();
    }
    is_on_ = false;
    ModuleBase::end();
  }

 private:
  bool applyLight(bool on) {
    if (context_.hardware == nullptr) {
      return false;
    }
    if (!on || level_ == 0U) {
      context_.hardware->clearManualLed();
      is_on_ = false;
      updateStatusEvent();
      return true;
    }
    const uint8_t safe_level = (level_ > kMaxSafeLevel) ? kMaxSafeLevel : level_;
    if (!context_.hardware->setManualLed(255U, 255U, 255U, safe_level, false)) {
      return false;
    }
    level_ = safe_level;
    is_on_ = true;
    updateStatusEvent();
    return true;
  }

  void updateStatusEvent() {
    char event[40] = {0};
    std::snprintf(event,
                  sizeof(event),
                  "on=%u level=%u",
                  is_on_ ? 1U : 0U,
                  static_cast<unsigned int>(level_));
    core::copyText(status_.last_event, sizeof(status_.last_event), event);
  }

  uint8_t level_ = 120U;
  bool is_on_ = false;
};

class ExpressionParser {
 public:
  explicit ExpressionParser(const char* text) : text_(text != nullptr ? text : "") {}

  bool parse(double* out_value) {
    cursor_ = text_;
    const bool ok = parseExpr(out_value);
    skipSpaces();
    return ok && *cursor_ == '\0';
  }

 private:
  void skipSpaces() {
    while (*cursor_ == ' ' || *cursor_ == '\t' || *cursor_ == '\n' || *cursor_ == '\r') {
      ++cursor_;
    }
  }

  bool parseExpr(double* out) {
    if (!parseTerm(out)) {
      return false;
    }
    for (;;) {
      skipSpaces();
      if (*cursor_ == '+') {
        ++cursor_;
        double rhs = 0.0;
        if (!parseTerm(&rhs)) {
          return false;
        }
        *out += rhs;
      } else if (*cursor_ == '-') {
        ++cursor_;
        double rhs = 0.0;
        if (!parseTerm(&rhs)) {
          return false;
        }
        *out -= rhs;
      } else {
        return true;
      }
    }
  }

  bool parseTerm(double* out) {
    if (!parseFactor(out)) {
      return false;
    }
    for (;;) {
      skipSpaces();
      if (*cursor_ == '*') {
        ++cursor_;
        double rhs = 0.0;
        if (!parseFactor(&rhs)) {
          return false;
        }
        *out *= rhs;
      } else if (*cursor_ == '/') {
        ++cursor_;
        double rhs = 0.0;
        if (!parseFactor(&rhs) || rhs == 0.0) {
          return false;
        }
        *out /= rhs;
      } else {
        return true;
      }
    }
  }

  bool parseFactor(double* out) {
    skipSpaces();
    if (*cursor_ == '(') {
      ++cursor_;
      if (!parseExpr(out)) {
        return false;
      }
      skipSpaces();
      if (*cursor_ != ')') {
        return false;
      }
      ++cursor_;
      return true;
    }
    if (*cursor_ == '+' || *cursor_ == '-') {
      const char sign = *cursor_++;
      if (!parseFactor(out)) {
        return false;
      }
      if (sign == '-') {
        *out = -*out;
      }
      return true;
    }
    char* end = nullptr;
    const double value = std::strtod(cursor_, &end);
    if (end == cursor_) {
      return false;
    }
    *out = value;
    cursor_ = end;
    return true;
  }

  const char* text_ = "";
  const char* cursor_ = "";
};

class CalculatorModule : public ModuleBase {
 public:
  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "status")) {
      writeStatusEvent();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "clear")) {
      result_ = 0.0;
      setError("");
      writeStatusEvent();
      return;
    }
    if (!core::equalsIgnoreCase(action.name, "eval")) {
      setError("unsupported_action");
      return;
    }
    if (action.payload[0] == '\0') {
      setError("empty_expression");
      return;
    }
#if ZACUS_USE_TINYEXPR
    int error = 0;
    const double value = te_interp(action.payload, &error);
    if (error != 0) {
      setError("eval_error");
      char msg[40] = {0};
      std::snprintf(msg, sizeof(msg), "eval_error@%d", error);
      core::copyText(status_.last_event, sizeof(status_.last_event), msg);
      return;
    }
    result_ = value;
#else
    ExpressionParser parser(action.payload);
    double value = 0.0;
    if (!parser.parse(&value)) {
      setError("eval_error");
      return;
    }
    result_ = value;
#endif
    setError("");
    writeStatusEvent();
  }

 private:
  void writeStatusEvent() {
    char msg[40] = {0};
    std::snprintf(msg, sizeof(msg), "result=%.4f", result_);
    core::copyText(status_.last_event, sizeof(status_.last_event), msg);
  }

  double result_ = 0.0;
};

class KidsCreativeModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.descriptor == nullptr || context.storage == nullptr) {
      setError("creative_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    app_id_ = context.descriptor->id;
    ensureDir("/apps");
    String app_dir = "/apps/" + app_id_;
    ensureDir(app_dir.c_str());
    ensureDir((app_dir + "/content").c_str());
    loadCanvas(String("/apps/") + app_id_ + "/content/last_canvas.json");
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    DynamicJsonDocument body(384);
    (void)parseJsonPayload(action, &body);
    if (core::equalsIgnoreCase(action.name, "open_canvas")) {
      if (body["color"].is<const char*>()) {
        current_color_ = body["color"].as<const char*>();
      }
      setError("");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "stroke")) {
      ++stroke_count_;
      if (body["color"].is<const char*>()) {
        current_color_ = body["color"].as<const char*>();
      } else if (action.payload[0] != '\0' && action.payload[0] != '{') {
        current_color_ = action.payload;
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "fill")) {
      if (body["color"].is<const char*>()) {
        current_color_ = body["color"].as<const char*>();
      } else if (action.payload[0] != '\0') {
        current_color_ = action.payload;
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "undo")) {
      if (stroke_count_ > 0U) {
        --stroke_count_;
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "clear") || core::equalsIgnoreCase(action.name, "clear_canvas")) {
      stroke_count_ = 0U;
      current_color_ = "#000000";
      return;
    }
    if (core::equalsIgnoreCase(action.name, "save")) {
      String target = (action.payload[0] != '\0' && action.payload[0] != '{') ? String(action.payload) : String();
      if (body["path"].is<const char*>()) {
        target = body["path"].as<const char*>();
      }
      if (target.isEmpty()) {
        target = String("/apps/") + app_id_ + "/content/last_canvas.json";
      }
      if (!saveCanvas(target)) {
        setError("canvas_save_failed");
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "load")) {
      String source = (action.payload[0] != '\0' && action.payload[0] != '{') ? String(action.payload) : String();
      if (body["path"].is<const char*>()) {
        source = body["path"].as<const char*>();
      }
      if (source.isEmpty()) {
        source = String("/apps/") + app_id_ + "/content/last_canvas.json";
      }
      if (!loadCanvas(source)) {
        setError("canvas_load_failed");
      }
      return;
    }
  }

 private:
  bool saveCanvas(const String& path) {
    DynamicJsonDocument doc(512);
    doc["id"] = app_id_;
    doc["updated_at_ms"] = millis();
    doc["stroke_count"] = stroke_count_;
    doc["color"] = current_color_;
    String payload;
    serializeJson(doc, payload);
    return writeTextFile(path.c_str(), payload);
  }

  bool loadCanvas(const String& path) {
    const String payload = readTextFile(path.c_str());
    if (payload.isEmpty()) {
      return false;
    }
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
      return false;
    }
    stroke_count_ = doc["stroke_count"] | 0U;
    current_color_ = doc["color"] | "#000000";
    return true;
  }

  String app_id_;
  uint32_t stroke_count_ = 0U;
  String current_color_ = "#000000";
};

class KidsLearningModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.descriptor == nullptr || context.storage == nullptr) {
      setError("learning_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    app_id_ = context.descriptor->id;
    ensureDir("/apps");
    String app_dir = "/apps/" + app_id_;
    ensureDir(app_dir.c_str());
    ensureDir((app_dir + "/content").c_str());
    ensureDir((app_dir + "/audio").c_str());
    loadProgress();
    return true;
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    DynamicJsonDocument body(512);
    (void)parseJsonPayload(action, &body);
    if (core::equalsIgnoreCase(action.name, "lesson_open")) {
      if (body["lesson"].is<const char*>()) {
        lesson_id_ = body["lesson"].as<const char*>();
      } else if (action.payload[0] != '\0' && action.payload[0] != '{') {
        lesson_id_ = action.payload;
      }
      lesson_step_ = 0U;
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "lesson_next")) {
      lesson_step_ += 1U;
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "lesson_prev")) {
      if (lesson_step_ > 0U) {
        lesson_step_ -= 1U;
      }
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "quiz_answer")) {
      String answer;
      if (body["answer"].is<const char*>()) {
        answer = body["answer"].as<const char*>();
      } else if (action.payload[0] != '\0' && action.payload[0] != '{') {
        answer = action.payload;
      }
      answer.toLowerCase();
      if (answer == "1" || answer == "true" || answer == "ok" || answer == "correct") {
        score_ += 1U;
      }
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "session_start") || core::equalsIgnoreCase(action.name, "play")) {
      startGuidedAudio(action.payload, body);
      return;
    }
    if (core::equalsIgnoreCase(action.name, "pause")) {
      if (context_.audio != nullptr) {
        context_.audio->stop();
      }
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "session_stop") || core::equalsIgnoreCase(action.name, "stop")) {
      if (context_.media != nullptr) {
        context_.media->stop(context_.audio);
      } else if (context_.audio != nullptr) {
        context_.audio->stop();
      }
      saveProgress();
      return;
    }
    if (core::equalsIgnoreCase(action.name, "seek_ms")) {
      cursor_ms_ = parseUint(action.payload, cursor_ms_);
      saveProgress();
      return;
    }
  }

  void end() override {
    saveProgress();
    ModuleBase::end();
  }

 private:
  void startGuidedAudio(const char* payload, const DynamicJsonDocument& body) {
    if (context_.audio == nullptr) {
      setError("audio_context_missing");
      return;
    }
    String target;
    if (body["path"].is<const char*>()) {
      target = body["path"].as<const char*>();
    } else if (payload != nullptr && payload[0] != '\0' && payload[0] != '{') {
      target = payload;
    } else {
      target = String("/apps/") + app_id_ + "/audio/session.mp3";
      if (!LittleFS.exists(target.c_str())) {
        target = defaultBundledAudioForApp(app_id_.c_str());
      }
    }
    bool ok = false;
    if (startsWithIgnoreCase(target.c_str(), "http://") || startsWithIgnoreCase(target.c_str(), "https://")) {
      if (context_.network != nullptr) {
        const NetworkManager::Snapshot net = context_.network->snapshot();
        if (!net.sta_connected) {
          target = String("/apps/") + app_id_ + "/audio/session.mp3";
        }
      }
      if (startsWithIgnoreCase(target.c_str(), "http://") || startsWithIgnoreCase(target.c_str(), "https://")) {
        ok = context_.audio->play(target.c_str());
      } else if (context_.storage != nullptr && context_.storage->fileExists(target.c_str()) && context_.media != nullptr) {
        ok = context_.media->play(target.c_str(), context_.audio);
      }
    } else if (context_.media != nullptr) {
      if (context_.storage != nullptr && !context_.storage->fileExists(target.c_str())) {
        setError("missing_asset");
        return;
      }
      ok = context_.media->play(target.c_str(), context_.audio);
    } else {
      ok = context_.audio->play(target.c_str());
    }
    if (!ok) {
      setError(startsWithIgnoreCase(target.c_str(), "http://") || startsWithIgnoreCase(target.c_str(), "https://")
                   ? "network_unavailable"
                   : "missing_asset");
      return;
    }
    setError("");
  }

  void loadProgress() {
    const String path = String("/apps/") + app_id_ + "/progress.json";
    const String payload = readTextFile(path.c_str());
    if (payload.isEmpty()) {
      return;
    }
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
      return;
    }
    lesson_id_ = doc["cursor"]["lesson"] | "";
    lesson_step_ = doc["cursor"]["step"] | 0U;
    score_ = doc["cursor"]["score"] | 0U;
    cursor_ms_ = doc["cursor"]["cursor_ms"] | 0U;
  }

  void saveProgress() {
    const String path = String("/apps/") + app_id_ + "/progress.json";
    DynamicJsonDocument doc(640);
    doc["id"] = app_id_;
    doc["updated_at_ms"] = millis();
    JsonObject cursor = doc["cursor"].to<JsonObject>();
    cursor["lesson"] = lesson_id_;
    cursor["step"] = lesson_step_;
    cursor["score"] = score_;
    cursor["cursor_ms"] = cursor_ms_;
    String payload;
    serializeJson(doc, payload);
    if (!writeTextFile(path.c_str(), payload)) {
      setError("progress_save_failed");
    }
  }

  String app_id_;
  String lesson_id_;
  uint32_t lesson_step_ = 0U;
  uint32_t score_ = 0U;
  uint32_t cursor_ms_ = 0U;
};

class NesEmulatorModule : public ModuleBase {
 public:
  bool begin(const AppContext& context) override {
    if (!ModuleBase::begin(context)) {
      return false;
    }
    if (context.storage == nullptr) {
      setError("storage_context_missing");
      status_.state = AppRuntimeState::kFailed;
      return false;
    }
    ensureDir("/apps");
    ensureDir("/apps/nes_emulator");
    ensureDir("/apps/nes_emulator/roms");
    emu_running_ = false;
    frame_count_ = 0U;
    input_mask_ = 0U;
    core_cycle_budget_ = 0U;
    return true;
  }

  void tick(uint32_t now_ms) override {
    ModuleBase::tick(now_ms);
    if (!emu_running_) {
      return;
    }
    if (static_cast<int32_t>(now_ms - next_frame_ms_) < 0) {
      return;
    }
    const uint32_t frame_interval = (target_fps_ == 0U) ? 16U : (1000U / target_fps_);
    next_frame_ms_ = now_ms + (frame_interval == 0U ? 1U : frame_interval);
    runCoreFrame();
    ++frame_count_;
    if ((frame_count_ % 60U) == 0U) {
      char event[40] = {0};
      std::snprintf(event,
                    sizeof(event),
                    "fps=%lu",
                    static_cast<unsigned long>(target_fps_));
      core::copyText(status_.last_event, sizeof(status_.last_event), event);
    }
  }

  void handleAction(const AppAction& action) override {
    ModuleBase::handleAction(action);
    if (core::equalsIgnoreCase(action.name, "list_roms")) {
      uint32_t count = 0U;
      File dir = LittleFS.open("/apps/nes_emulator/roms", "r");
      if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
          const String path = entry.name();
          if (!entry.isDirectory() && endsWithIgnoreCase(path.c_str(), ".nes")) {
            ++count;
          }
          entry.close();
          entry = dir.openNextFile();
        }
        dir.close();
      }
      char event[40] = {0};
      std::snprintf(event, sizeof(event), "roms=%lu", static_cast<unsigned long>(count));
      core::copyText(status_.last_event, sizeof(status_.last_event), event);
      return;
    }

    DynamicJsonDocument body(640);
    const bool has_json = parseJsonPayload(action, &body);
    String rom_path = action.payload;
    if (has_json && body["path"].is<const char*>()) {
      rom_path = body["path"].as<const char*>();
    }

    if (core::equalsIgnoreCase(action.name, "rom_validate")) {
      validateRom(rom_path.c_str());
      return;
    }
    if (core::equalsIgnoreCase(action.name, "rom_start")) {
      if (!validateRom(rom_path.c_str())) {
        return;
      }
      if (!loadRomImage(rom_path.c_str())) {
        setError("rom_load_failed");
        return;
      }
      if (has_json && body["fps"].is<uint32_t>()) {
        const uint32_t requested_fps = body["fps"].as<uint32_t>();
        target_fps_ = (requested_fps < 30U) ? 30U : (requested_fps > 60U ? 60U : requested_fps);
      } else {
        target_fps_ = 60U;
      }
      emu_running_ = true;
      next_frame_ms_ = millis();
      frame_count_ = 0U;
      setError("");
      core::copyText(status_.last_event, sizeof(status_.last_event), "rom_start");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "rom_stop")) {
      emu_running_ = false;
      rom_buffer_.clear();
      rom_buffer_.shrink_to_fit();
      setError("");
      core::copyText(status_.last_event, sizeof(status_.last_event), "rom_stop");
      return;
    }
    if (core::equalsIgnoreCase(action.name, "set_fps")) {
      const uint32_t requested = parseUint(action.payload, target_fps_);
      target_fps_ = (requested < 30U) ? 30U : (requested > 60U ? 60U : requested);
      return;
    }
    if (core::equalsIgnoreCase(action.name, "input") ||
        core::equalsIgnoreCase(action.name, "btn_down") ||
        core::equalsIgnoreCase(action.name, "btn_up")) {
      uint16_t mask = input_mask_;
      if (has_json && body["mask"].is<uint16_t>()) {
        mask = body["mask"].as<uint16_t>();
      } else {
        mask = static_cast<uint16_t>(parseUint(action.payload, input_mask_));
      }
      if (core::equalsIgnoreCase(action.name, "btn_down")) {
        input_mask_ |= mask;
      } else if (core::equalsIgnoreCase(action.name, "btn_up")) {
        input_mask_ &= static_cast<uint16_t>(~mask);
      } else {
        input_mask_ = mask;
      }
      return;
    }
    if (core::equalsIgnoreCase(action.name, "core_status")) {
      char event[56] = {0};
      std::snprintf(event,
                    sizeof(event),
                    "run=%u frames=%lu",
                    emu_running_ ? 1U : 0U,
                    static_cast<unsigned long>(frame_count_));
      core::copyText(status_.last_event, sizeof(status_.last_event), event);
      return;
    }
  }

  void end() override {
    emu_running_ = false;
    rom_buffer_.clear();
    rom_buffer_.shrink_to_fit();
    ModuleBase::end();
  }

 private:
  bool validateRom(const char* rom_path) {
    if (rom_path == nullptr || rom_path[0] == '\0') {
      setError("rom_path_missing");
      return false;
    }
    if (!startsWithIgnoreCase(rom_path, "/apps/nes_emulator/roms/")) {
      setError("rom_path_forbidden");
      return false;
    }
    if (!endsWithIgnoreCase(rom_path, ".nes")) {
      setError("rom_ext_invalid");
      return false;
    }
    if (context_.storage == nullptr || !context_.storage->fileExists(rom_path)) {
      setError("rom_missing");
      return false;
    }
    File file = LittleFS.open(rom_path, "r");
    if (!file || file.isDirectory()) {
      setError("rom_open_failed");
      return false;
    }
    uint8_t header[16] = {0};
    const size_t read = file.read(header, sizeof(header));
    file.close();
    if (read != sizeof(header) ||
        header[0] != 0x4EU ||
        header[1] != 0x45U ||
        header[2] != 0x53U ||
        header[3] != 0x1AU) {
      setError("rom_header_invalid");
      return false;
    }
    const uint8_t prg_banks = header[4];
    const uint8_t flags6 = header[6];
    const uint8_t flags7 = header[7];
    const uint8_t mapper = static_cast<uint8_t>((flags6 >> 4U) | (flags7 & 0xF0U));
    if (prg_banks == 0U) {
      setError("rom_prg_invalid");
      return false;
    }
    if ((flags6 & 0x04U) != 0U) {
      setError("rom_trainer_unsupported");
      return false;
    }
    if (mapper != 0U) {
      setError("rom_mapper_unsupported");
      return false;
    }
    setError("");
    core::copyText(status_.last_event, sizeof(status_.last_event), "rom_ok");
    return true;
  }

  bool loadRomImage(const char* rom_path) {
    if (rom_path == nullptr || rom_path[0] == '\0') {
      return false;
    }
    File file = LittleFS.open(rom_path, "r");
    if (!file || file.isDirectory()) {
      return false;
    }
    const size_t file_size = static_cast<size_t>(file.size());
    if (file_size < 16U || file_size > kMaxRomBytes) {
      file.close();
      setError("rom_size_unsupported");
      return false;
    }
    rom_buffer_.assign(file_size, 0U);
    const size_t read = file.read(rom_buffer_.data(), file_size);
    file.close();
    if (read != file_size) {
      rom_buffer_.clear();
      return false;
    }
    rom_path_ = rom_path;
    rom_hash_ = computeHash(rom_buffer_);
    return true;
  }

  static uint32_t computeHash(const std::vector<uint8_t>& data) {
    uint32_t hash = 2166136261UL;
    for (uint8_t value : data) {
      hash ^= static_cast<uint32_t>(value);
      hash *= 16777619UL;
    }
    return hash;
  }

  void runCoreFrame() {
    // Deterministic frame budget to keep UI loop non-blocking.
    core_cycle_budget_ += 29780U;
    if (input_mask_ != 0U) {
      core_cycle_budget_ += 23U;
    }
    if (core_cycle_budget_ > 2000000000UL) {
      core_cycle_budget_ = 0U;
    }
  }

  static constexpr size_t kMaxRomBytes = 1024U * 1024U;
  bool emu_running_ = false;
  uint32_t target_fps_ = 60U;
  uint32_t next_frame_ms_ = 0U;
  uint32_t frame_count_ = 0U;
  uint16_t input_mask_ = 0U;
  uint32_t core_cycle_budget_ = 0U;
  uint32_t rom_hash_ = 0U;
  String rom_path_;
  std::vector<uint8_t> rom_buffer_;
};

}  // namespace

std::unique_ptr<IAppModule> createAppModule(const AppDescriptor& descriptor) {
  if (core::equalsIgnoreCase(descriptor.id, "audio_player") ||
      core::equalsIgnoreCase(descriptor.id, "kids_webradio") ||
      core::equalsIgnoreCase(descriptor.id, "kids_podcast") ||
      core::equalsIgnoreCase(descriptor.id, "kids_music")) {
    return std::unique_ptr<IAppModule>(new AudioPlayerModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "audiobook_player")) {
    return std::unique_ptr<IAppModule>(new AudiobookModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "camera_video")) {
    return std::unique_ptr<IAppModule>(new CameraVideoModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "qr_scanner")) {
    return std::unique_ptr<IAppModule>(new QrScannerModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "dictaphone")) {
    return std::unique_ptr<IAppModule>(new DictaphoneModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "timer_tools")) {
    return std::unique_ptr<IAppModule>(new TimerToolsModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "flashlight")) {
    return std::unique_ptr<IAppModule>(new FlashlightModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "calculator")) {
    return std::unique_ptr<IAppModule>(new CalculatorModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "kids_drawing") ||
      core::equalsIgnoreCase(descriptor.id, "kids_coloring")) {
    return std::unique_ptr<IAppModule>(new KidsCreativeModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "kids_yoga") ||
      core::equalsIgnoreCase(descriptor.id, "kids_meditation") ||
      core::equalsIgnoreCase(descriptor.id, "kids_languages") ||
      core::equalsIgnoreCase(descriptor.id, "kids_math") ||
      core::equalsIgnoreCase(descriptor.id, "kids_science") ||
      core::equalsIgnoreCase(descriptor.id, "kids_geography")) {
    return std::unique_ptr<IAppModule>(new KidsLearningModule());
  }
  if (core::equalsIgnoreCase(descriptor.id, "nes_emulator")) {
    return std::unique_ptr<IAppModule>(new NesEmulatorModule());
  }
  return nullptr;
}

}  // namespace app::modules
