// app_module.h - Interface for pluggable app modules.
#pragma once

#include <Arduino.h>

struct RuntimeServices;
struct AppEntry;

enum class AppModuleState : uint8_t {
  kIdle = 0,
  kStarting,
  kRunning,
  kError,
};

class IAppModule {
 public:
  virtual ~IAppModule() = default;

  virtual const char* id() const = 0;

  // Called once when APP_OPEN arrives. Return true if launch succeeded.
  virtual bool onOpen(const AppEntry& entry, const char* mode, RuntimeServices* services) = 0;

  // Called once when APP_CLOSE arrives.
  virtual void onClose(const char* reason, RuntimeServices* services) = 0;

  // Called every frame while the app is running.
  virtual void onTick(uint32_t now_ms, RuntimeServices* services) = 0;

  // Dispatch an APP_ACTION. Return true if handled.
  virtual bool onAction(const char* action, const char* payload, RuntimeServices* services) = 0;

  // Current module state.
  virtual AppModuleState state() const = 0;

  // Last error (may be empty).
  virtual const char* lastError() const { return ""; }
};
