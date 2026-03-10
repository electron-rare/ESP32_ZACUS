// app_runtime_manager.h - App lifecycle: open/close/action state machine.
#pragma once

#include <Arduino.h>
#include "app/app_module.h"
#include "app/app_registry.h"

struct RuntimeServices;

class AppRuntimeManager {
 public:
  enum class State : uint8_t {
    kIdle = 0,
    kStarting,
    kRunning,
    kClosing,
    kError,
  };

  struct Snapshot {
    State state = State::kIdle;
    const char* app_id = "";
    const char* entry_screen = "";
    const char* last_error = "";
    uint32_t opened_at_ms = 0U;
  };

  static constexpr uint8_t kMaxModules = 24U;

  AppRuntimeManager() = default;

  void begin(AppRegistry* registry, RuntimeServices* services);

  // Register a module (call during setup, before any open).
  bool registerModule(IAppModule* module);

  // App lifecycle.
  bool open(const char* app_id, const char* mode, uint32_t now_ms);
  bool close(const char* reason, uint32_t now_ms);
  bool action(const char* action_name, const char* payload);
  void tick(uint32_t now_ms);

  // Status.
  State state() const { return state_; }
  Snapshot snapshot() const;
  bool isRunning() const { return state_ == State::kRunning; }
  bool isIdle() const { return state_ == State::kIdle; }
  const char* currentAppId() const { return current_app_id_; }

 private:
  IAppModule* findModule(const char* app_id) const;
  void transitionTo(State next, const char* reason);

  AppRegistry* registry_ = nullptr;
  RuntimeServices* services_ = nullptr;

  IAppModule* modules_[kMaxModules] = {nullptr};
  uint8_t module_count_ = 0U;

  IAppModule* active_module_ = nullptr;
  State state_ = State::kIdle;
  char current_app_id_[24] = {0};
  char current_entry_screen_[40] = {0};
  char last_error_[64] = {0};
  uint32_t opened_at_ms_ = 0U;
  uint32_t close_requested_ms_ = 0U;
};
