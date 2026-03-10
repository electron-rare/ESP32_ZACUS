// ui_amiga_shell.cpp - Amiga UI Shell Implementation
#include "ui/ui_amiga_shell.h"
#include "hardware_manager.h"
#include "ui_manager.h"

AmigaUIShell g_amiga_shell;

// App catalog - 7 core apps with Amiga theme colors
const AmigaUIShell::AppIcon AmigaUIShell::APPS[7] = {
    {"Audio", "audio_player", 0x00FFFF},     // Cyan speaker
    {"Calculate", "calculator", 0xFFFF00},   // Yellow numbers
    {"Timer", "timer_tools", 0xFF00FF},      // Magenta clock
    {"Light", "flashlight", 0xFFFF00},       // Yellow torch
    {"Camera", "camera_video", 0x0088FF},    // Blue lens
    {"Mic", "dictaphone", 0x00FF88},         // Green waves
    {"QR", "qr_scanner", 0xFFFF00},          // Yellow scanner
};

bool AmigaUIShell::init(HardwareManager* hw, UiManager* ui) {
  hardware_ = hw;
  ui_ = ui;
  
  Serial.println("[UI_AMIGA] Initialized Amiga shell");
  return true;
}

void AmigaUIShell::onStart() {
  selected_index_ = 0;
  animation_elapsed_ms_ = 0;
  animating_ = true;
  
  drawMainMenu();
  Serial.println("[UI_AMIGA] Main menu displayed");
}

void AmigaUIShell::onStop() {
  animating_ = false;
  Serial.println("[UI_AMIGA] Shell stopped");
}

void AmigaUIShell::onTick(uint32_t dt_ms) {
  // Update animation
  if (animating_) {
    animation_elapsed_ms_ += dt_ms;
    
    // Redraw with animation
    drawMainMenu();
    
    // Animation complete after 300ms
    if (animation_elapsed_ms_ >= 300) {
      animating_ = false;
    }
  }
}

void AmigaUIShell::selectApp(uint8_t grid_index) {
  if (grid_index < 7) {  // We have 7 apps
    selected_index_ = grid_index;
    animating_ = true;
    animation_elapsed_ms_ = 0;
    
    Serial.printf("[UI_AMIGA] Selected: %s (%u)\n", APPS[grid_index].name, grid_index);
    
    // Flash effect on selection
    for (int i = 0; i < 2; i++) {
      delay(100);
    }
  }
}

void AmigaUIShell::launchSelectedApp() {
  if (selected_index_ < 7) {
    const AppIcon& app = APPS[selected_index_];
    Serial.printf("[UI_AMIGA] Launching: %s\n", app.app_id);
    
    // Transition effect
    playTransitionFX();
    
    // TODO: Dispatch to AppCoordinator to launch app
  }
}

void AmigaUIShell::drawMainMenu() {
  // Clear with black background (Amiga style)
  // In real implementation, would use LVGL: lv_obj_set_style_bg_color(...)
  
  Serial.println("[UI_AMIGA] Drawing main menu");
  
  // Draw grid of 7 apps (could expand to 4x4 = 16 later)
  uint16_t start_x = 16;
  uint16_t start_y = 32;
  
  for (uint8_t i = 0; i < 7; i++) {
    uint16_t col = i % GRID_COLS;
    uint16_t row = i / GRID_COLS;
    
    uint16_t x = start_x + (col * (ICON_SIZE + ICON_SPACING));
    uint16_t y = start_y + (row * (ICON_SIZE + ICON_SPACING));
    
    bool selected = (i == selected_index_);
    drawIcon(x, y, APPS[i], selected);
  }
}

void AmigaUIShell::drawIcon(uint16_t x, uint16_t y, const AppIcon& icon, bool selected) {
  // Draw colored square with app name
  uint32_t color = selected ? 0x00FFFF : icon.color;  // Cyan when selected
  
  Serial.printf("[UI_AMIGA] Icon: %s at (%u,%u) color=%06X %s\n",
                icon.name, x, y, color, selected ? "(selected)" : "");
  
  // In real LVGL implementation:
  // - Draw rectangle with rounded corners
  // - Fill with color
  // - Add text label
  // - Draw border if selected
  
  if (selected) {
    // Draw pulse effect for selected icons
    drawPulseEffect(x, y, 1.0f);
  }
}

void AmigaUIShell::drawPulseEffect(uint16_t x, uint16_t y, float intensity) {
  // Pulse animation for selected item
  float pulse = sin(animation_elapsed_ms_ * 0.01f) * 0.5f + 0.5f;
  float scale = 1.0f + (pulse * 0.1f);
  
  // Serial output for debugging (in real impl would use LVGL transforms)
  Serial.printf("[UI_AMIGA] Pulse effect: scale=%.2f\n", scale);
}

void AmigaUIShell::drawSelectionHighlight(uint8_t index) {
  if (index < 7) {
    uint16_t col = index % GRID_COLS;
    uint16_t row = index / GRID_COLS;
    
    uint16_t x = 16 + (col * (ICON_SIZE + ICON_SPACING));
    uint16_t y = 32 + (row * (ICON_SIZE + ICON_SPACING));
    
    Serial.printf("[UI_AMIGA] Selection highlight at (%u, %u)\n", x, y);
    
    // Draw bright cyan border
    // In LVGL: lv_objset_style_border_color(..., COLOR_CYAN)
  }
}

void AmigaUIShell::playTransitionFX() {
  // Fade + slide transition effect
  Serial.println("[UI_AMIGA] Playing transition FX (fade + slide)");
  
  // 300ms fadeout
  for (uint8_t opacity = 255; opacity > 0; opacity -= 25) {
    drawFadeTransition(opacity);
    delay(30);
  }
  
  // Transition complete
  Serial.println("[UI_AMIGA] Transition complete");
}

void AmigaUIShell::drawFadeTransition(uint8_t opacity) {
  // Fade overlay effect
  Serial.printf("[UI_AMIGA] Fade: opacity=%u/255\n", opacity);
  
  // In LVGL: lv_obj_set_style_opa(overlay, opacity, LV_PART_MAIN)
}
