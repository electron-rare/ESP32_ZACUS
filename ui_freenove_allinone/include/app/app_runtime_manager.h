// app_runtime_manager.h - app lifecycle and capability gate.
#pragma once

#include <memory>

#include "app/app_registry.h"
#include "app/app_runtime_types.h"

class AppRuntimeManager {
 public:
  AppRuntimeManager() = default;

  void configure(AppRegistry* registry, const AppContext& context);
  bool startApp(const AppStartRequest& request, uint32_t now_ms);
  bool stopApp(const AppStopRequest& request, uint32_t now_ms);
  bool handleAction(const AppAction& action, uint32_t now_ms);
  void tick(uint32_t now_ms);

  AppRuntimeStatus current() const;
  const AppDescriptor* currentDescriptor() const;

 private:
  uint32_t evaluateMissingCapabilities(const AppDescriptor& descriptor) const;
  static void copyText(char* out, size_t out_size, const char* text);

  AppRegistry* registry_ = nullptr;
  AppContext context_ = {};
  std::unique_ptr<IAppModule> module_;
  const AppDescriptor* current_descriptor_ = nullptr;
  AppRuntimeStatus status_ = {};
};
