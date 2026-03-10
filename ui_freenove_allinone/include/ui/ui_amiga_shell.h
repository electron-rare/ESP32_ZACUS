// ui_amiga_shell.h - Amiga Retro Theme UI Shell
// Child-friendly grid-based launcher with nostalgic Amiga demoscene aesthetic
#pragma once

#include "ui_manager.h"
#include <Arduino.h>

class AmigaUIShell {
 public:
  bool init(HardwareManager* hw, UiManager* ui);
  void onStart();
  void onStop();
  void onTick(uint32_t dt_ms);
  
  // Navigation
  void selectApp(uint8_t grid_index);
  void launchSelectedApp();
  
  // Visual
  void drawMainMenu();
  void drawAppGrid();
  void drawSelectionHighlight(uint8_t index);
  void playTransitionFX();
  
 private:
  HardwareManager* hardware_ = nullptr;
  UiManager* ui_ = nullptr;
  
  // Current state
  uint8_t selected_index_ = 0;  // 0-15 for 4x4 grid
  uint32_t animation_elapsed_ms_ = 0;
  bool animating_ = false;
  
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
    const char* name;
    const char* app_id;
    uint32_t color;
  };
  
  // App catalog (7 core apps)
  static const AppIcon APPS[7];
  
  void drawIcon(uint16_t x, uint16_t y, const AppIcon& icon, bool selected);
  void drawPulseEffect(uint16_t x, uint16_t y, float intensity);
  void drawFadeTransition(uint8_t opacity);
};

extern AmigaUIShell g_amiga_shell;
