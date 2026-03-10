// ui_amiga_shell.h - Amiga Retro Theme UI Shell
// Child-friendly grid-based launcher with nostalgic Amiga demoscene aesthetic
#pragma once

#include "ui_manager.h"
#include "app/app_runtime_types.h"
#include "drivers/input/touch_emulator.h"
#include <Arduino.h>
#include <vector>

class AppRegistry;
class TouchManager;

struct AmigaAppRuntimeBridge {
  bool (*open_app)(const char* app_id, const char* mode, const char* source);
  bool (*close_app)(const char* reason);
  AppRuntimeStatus (*current_status)();
};

class AmigaUIShell {
 public:
  bool init(HardwareManager* hw, UiManager* ui, AppRegistry* registry);
  void setRuntimeBridge(const AmigaAppRuntimeBridge* bridge);
  void setTouchManager(TouchManager* touch_mgr);  // For touch emulation integration
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  
  // Touch emulation mode
  void setTouchEmulationMode(bool enabled);
  bool isTouchEmulationEnabled() const { return enable_touch_emulation_; }
  
  // Navigation
  void selectApp(uint8_t grid_index);
  void launchSelectedApp();
  void handleTouchInput(uint16_t x, uint16_t y);
  void handleButtonInput(uint8_t button_id);
  uint8_t getTouchGridIndex(uint16_t x, uint16_t y);
  
  // Visual
  void drawMainMenu();
  void drawAppGrid();
  void drawSelectionHighlight(uint8_t index);
  void playTransitionFX();

  // Runtime bridge
  bool requestOpenApp(const char* app_id, const char* mode, const char* source);
  bool requestCloseApp(const char* reason);
  AppRuntimeStatus currentStatus() const;
  
 private:
  HardwareManager* hardware_ = nullptr;
  UiManager* ui_ = nullptr;
  AppRegistry* registry_ = nullptr;
  TouchManager* touch_manager_ = nullptr;
  const AmigaAppRuntimeBridge* runtime_bridge_ = nullptr;
  
  // Touch emulation
  TouchEmulator touch_emulator_;
  bool enable_touch_emulation_ = false;
  bool cursor_direction_toggle_ = false;  // false=LEFT, true=RIGHT for button 4
  
  // Current state
  uint8_t selected_index_ = 0;  // 0-15 for 4x4 grid
  uint32_t animation_elapsed_ms_ = 0;
  bool animating_ = false;
  uint32_t last_launch_ms_ = 0U;
  static constexpr uint32_t LAUNCH_DEBOUNCE_MS = 450U;
  
  // Theme colors (Amiga neon)
  static constexpr uint32_t COLOR_CYAN = 0x00FFFF;
  static constexpr uint32_t COLOR_MAGENTA = 0xFF00FF;
  static constexpr uint32_t COLOR_YELLOW = 0xFFFF00;
  static constexpr uint32_t COLOR_BLACK = 0x000000;
  
  // Layout
  static constexpr uint8_t GRID_COLS = 4;
  static constexpr uint8_t GRID_ROWS = 4;
  static constexpr uint16_t ICON_SIZE = 64;
  static constexpr uint16_t ICON_SPACING = 16;
  
  struct AppIcon {
    String name;
    String app_id;
    uint32_t color;
  };
  
  // App catalog (dynamically loaded from registry)
  std::vector<AppIcon> apps_;
  
  void loadAppsFromRegistry();
  uint32_t getAppColor(size_t index) const;
  
  void drawIcon(uint16_t x, uint16_t y, const AppIcon& icon, bool selected);
  void drawPulseEffect(uint16_t x, uint16_t y, float intensity);
  void drawFadeTransition(uint8_t opacity);
  
  // Grid-to-pixel offset calculation
  static constexpr uint16_t GRID_START_X = 16;
  static constexpr uint16_t GRID_START_Y = 32;
};

extern AmigaUIShell g_amiga_shell;
