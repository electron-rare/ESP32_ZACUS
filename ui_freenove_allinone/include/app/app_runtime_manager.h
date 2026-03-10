// app_runtime_manager.h - app lifecycle orchestration.
#pragma once

#include <cstdint>
#include <memory>

#include "app/app_registry.h"
#include "app/app_runtime_types.h"

class AppRuntimeManager {
 public:
  AppRuntimeManager() = default;

  // New runtime API.
  void configure(AppRegistry* registry, const AppContext& context);
  bool startApp(const AppStartRequest& request, uint32_t now_ms);
  bool stopApp(const AppStopRequest& request, uint32_t now_ms);
  bool handleAction(const AppAction& action, uint32_t now_ms);
  void tick(uint32_t now_ms);
  AppRuntimeStatus current() const;
  const AppDescriptor* currentDescriptor() const;

  // Legacy compatibility API (used by workbench shell).
  bool open(const char* app_id, const char* mode, uint32_t now_ms);
  bool close(const char* reason, uint32_t now_ms);
  bool action(const char* action_name, const char* payload);
  bool isRunning() const { return status_.state == AppRuntimeState::kRunning; }
  bool isIdle() const { return status_.state == AppRuntimeState::kIdle; }

 private:
  uint32_t evaluateMissingCapabilities(const AppDescriptor& descriptor) const;

  AppRegistry* registry_ = nullptr;
  AppContext context_ = {};
  std::unique_ptr<IAppModule> module_;
  const AppDescriptor* current_descriptor_ = nullptr;
  AppRuntimeStatus status_ = {};
};
