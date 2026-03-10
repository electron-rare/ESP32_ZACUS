// ui_amiga_shell.cpp - Amiga UI Shell Implementation
#include "ui/ui_amiga_shell.h"
#include "hardware_manager.h"
#include "ui_manager.h"
#include "app/app_registry.h"

AmigaUIShell g_amiga_shell;

bool AmigaUIShell::init(HardwareManager* hw, UiManager* ui, AppRegistry* registry) {
  hardware_ = hw;
  ui_ = ui;
  registry_ = registry;
  
  loadAppsFromRegistry();
  Serial.printf("[UI_AMIGA] Initialized Amiga shell with %u apps\n", apps_.size());
  return true;
}

void AmigaUIShell::loadAppsFromRegistry() {
  apps_.clear();
  
  if (registry_ == nullptr) {
    Serial.println("[UI_AMIGA] Warning: No registry provided, using empty catalog");
    return;
  }
  
  const auto& descriptors = registry_->descriptors();
  
  for (const auto& desc : descriptors) {
    if (desc.enabled) {
      AppIcon icon;
      icon.name = String(desc.title);
      icon.app_id = String(desc.id);
      icon.color = getAppColor(apps_.size());
      apps_.push_back(icon);
      
      Serial.printf("[UI_AMIGA] Loaded app: %s (%s) color=%06X\n",
                    icon.name.c_str(), icon.app_id.c_str(), icon.color);
    }
  }
  
  Serial.printf("[UI_AMIGA] Loaded %u enabled apps from registry\n", apps_.size());
}

uint32_t AmigaUIShell::getAppColor(size_t index) const {
  // Cycle through Amiga theme colors (cyan, yellow, magenta, blue, green)
  static const uint32_t AMIGA_COLORS[] = {
    0x00FFFF,  // Cyan
    0xFFFF00,  // Yellow
    0xFF00FF,  // Magenta
    0x0088FF,  // Blue
    0x00FF88,  // Green
    0xFF8800,  // Orange
    0xFF0088,  // Pink
    0x88FF00,  // Lime
  };
  return AMIGA_COLORS[index % (sizeof(AMIGA_COLORS) / sizeof(AMIGA_COLORS[0]))];
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
  if (grid_index < apps_.size()) {
    selected_index_ = grid_index;
    animating_ = true;
    animation_elapsed_ms_ = 0;
    
    Serial.printf("[UI_AMIGA] Selected: %s (%u)\n", apps_[grid_index].name.c_str(), grid_index);
    
    // Flash effect on selection
    for (int i = 0; i < 2; i++) {
      delay(100);
    }
  }
}

void AmigaUIShell::launchSelectedApp() {
  if (selected_index_ < apps_.size()) {
    const AppIcon& app = apps_[selected_index_];
    Serial.printf("[UI_AMIGA] Launching: %s\n", app.app_id.c_str());
    
    // Transition effect
    playTransitionFX();
    
    // TODO: Dispatch to AppCoordinator to launch app
  }
}

void AmigaUIShell::drawMainMenu() {
  // Clear with black background (Amiga style)
  // In real implementation, would use LVGL: lv_obj_set_style_bg_color(...)
  
  Serial.printf("[UI_AMIGA] Drawing main menu (%u apps)\n", apps_.size());
  
  // Draw grid of apps (could expand to 4x4 = 16 later)
  uint16_t start_x = 16;
  uint16_t start_y = 32;
  
  for (size_t i = 0; i < apps_.size(); i++) {
    uint16_t col = i % GRID_COLS;
    uint16_t row = i / GRID_COLS;
    
    uint16_t x = start_x + (col * (ICON_SIZE + ICON_SPACING));
    uint16_t y = start_y + (row * (ICON_SIZE + ICON_SPACING));
    
    bool selected = (i == selected_index_);
    drawIcon(x, y, apps_[i], selected);
  }
}

void AmigaUIShell::drawIcon(uint16_t x, uint16_t y, const AppIcon& icon, bool selected) {
  // Draw colored square with app name
  uint32_t color = selected ? 0x00FFFF : icon.color;  // Cyan when selected
  
  Serial.printf("[UI_AMIGA] Icon: %s at (%u,%u) color=%06X %s\n",
                icon.name.c_str(), x, y, color, selected ? "(selected)" : "");
  
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
  if (index < apps_.size()) {
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

uint8_t AmigaUIShell::getTouchGridIndex(uint16_t x, uint16_t y) {
  // Map display coordinates to grid index (0-15)
  // Grid is 4x4 with icons at fixed positions starting from (GRID_START_X, GRID_START_Y)
  
  // Check if touch is within grid bounds
  uint16_t grid_end_x = GRID_START_X + (GRID_COLS * (ICON_SIZE + ICON_SPACING));
  uint16_t grid_end_y = GRID_START_Y + (GRID_ROWS * (ICON_SIZE + ICON_SPACING));
  
  if (x < GRID_START_X || x >= grid_end_x ||
      y < GRID_START_Y || y >= grid_end_y) {
    return 255;  // Out of bounds
  }
  
  // Calculate relative position within grid
  uint16_t rel_x = x - GRID_START_X;
  uint16_t rel_y = y - GRID_START_Y;
  
  // Determine column and row
  uint8_t col = rel_x / (ICON_SIZE + ICON_SPACING);
  uint8_t row = rel_y / (ICON_SIZE + ICON_SPACING);
  
  // Clamp to valid grid bounds
  if (col >= GRID_COLS) col = GRID_COLS - 1;
  if (row >= GRID_ROWS) row = GRID_ROWS - 1;
  
  uint8_t index = row * GRID_COLS + col;
  
  // Only return valid indices for loaded apps
  return (index < apps_.size()) ? index : 255;
}

void AmigaUIShell::handleTouchInput(uint16_t x, uint16_t y) {
  uint8_t grid_index = getTouchGridIndex(x, y);
  
  if (grid_index < apps_.size()) {
    Serial.printf("[UI_AMIGA] Touch detected at grid[%u,%u] -> app index %u\n",
                  x, y, grid_index);
    selectApp(grid_index);
    
    // Auto-launch on tap (can be changed to "select-then-press-to-launch" later)
    delay(200);  // Brief visual feedback delay
    launchSelectedApp();
  } else {
    Serial.printf("[UI_AMIGA] Touch out of bounds: (%u,%u)\n", x, y);
  }
}

void AmigaUIShell::handleButtonInput(uint8_t button_id) {
  // Button mapping for grid navigation:
  // Button 0 (UP):     Move selection up (previous row)
  // Button 1 (SELECT): Launch selected app
  // Button 2 (DOWN):   Move selection down (next row)
  // Button 3 (MENU):   Future: return to launcher or show menu
  
  switch (button_id) {
    case 0: {  // UP button - move to previous row
      if (selected_index_ >= GRID_COLS) {
        selectApp(selected_index_ - GRID_COLS);
        Serial.printf("[UI_AMIGA] UP button: moved to index %u\n", selected_index_);
      }
      break;
    }
    
    case 1: {  // SELECT button - launch app
      if (selected_index_ < apps_.size()) {
        Serial.printf("[UI_AMIGA] SELECT button: launching app %u\n", selected_index_);
        launchSelectedApp();
      }
      break;
    }
    
    case 2: {  // DOWN button - move to next row
      if (selected_index_ + GRID_COLS < apps_.size()) {
        selectApp(selected_index_ + GRID_COLS);
        Serial.printf("[UI_AMIGA] DOWN button: moved to index %u\n", selected_index_);
      }
      break;
    }
    
    case 3: {  // MENU button - future use
      Serial.println("[UI_AMIGA] MENU button: reserved for future use");
      break;
    }
    
    default:
      Serial.printf("[UI_AMIGA] Unknown button: %u\n", button_id);
      break;
  }
}
