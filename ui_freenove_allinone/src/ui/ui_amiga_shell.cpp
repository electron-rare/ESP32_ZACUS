// ui_amiga_shell.cpp - Amiga UI Shell Implementation
#include "ui/ui_amiga_shell.h"
#include "hardware_manager.h"
#include "ui_manager.h"
#include "app/app_registry.h"
#include <cstring>

namespace {

const char* appRuntimeStateLabel(AppRuntimeState state) {
  switch (state) {
    case AppRuntimeState::kIdle:
      return "idle";
    case AppRuntimeState::kStarting:
      return "starting";
    case AppRuntimeState::kRunning:
      return "running";
    case AppRuntimeState::kStopping:
      return "stopping";
    case AppRuntimeState::kFailed:
      return "failed";
  }
  return "unknown";
}

}  // namespace

AmigaUIShell g_amiga_shell;

bool AmigaUIShell::init(HardwareManager* hw, UiManager* ui, AppRegistry* registry) {
  hardware_ = hw;
  ui_ = ui;
  registry_ = registry;
  
  loadAppsFromRegistry();
  Serial.printf("[UI_AMIGA] Initialized Amiga shell with %u apps\n", apps_.size());
  return true;
}

void AmigaUIShell::setRuntimeBridge(const AmigaAppRuntimeBridge* bridge) {
  runtime_bridge_ = bridge;
  Serial.printf("[UI_AMIGA] Runtime bridge attached=%u\n", runtime_bridge_ != nullptr ? 1U : 0U);
}

void AmigaUIShell::setTouchManager(TouchManager* touch_mgr) {
  touch_manager_ = touch_mgr;
  Serial.printf("[UI_AMIGA] Touch manager attached=%u\n", touch_manager_ != nullptr ? 1U : 0U);
}

void AmigaUIShell::setTouchEmulationMode(bool enabled) {
  enable_touch_emulation_ = enabled;
  if (enabled) {
    // Initialize emulator with grid layout matching AmigaUI shell
    touch_emulator_.begin(GRID_COLS, GRID_ROWS, 
                          ICON_SIZE + ICON_SPACING, ICON_SIZE + ICON_SPACING,
                          GRID_START_X, GRID_START_Y);
    touch_emulator_.setGridIndex(selected_index_);
    Serial.println("[UI_AMIGA] Touch emulation ENABLED");
  } else {
    Serial.println("[UI_AMIGA] Touch emulation DISABLED (button-only mode)");
  }
}

bool AmigaUIShell::requestOpenApp(const char* app_id, const char* mode, const char* source) {
  if (runtime_bridge_ == nullptr || runtime_bridge_->open_app == nullptr) {
    Serial.println("[UI_AMIGA] APP_OPEN_FAIL reason=bridge_unavailable");
    return false;
  }
  return runtime_bridge_->open_app(app_id, mode, source);
}

bool AmigaUIShell::requestCloseApp(const char* reason) {
  if (runtime_bridge_ == nullptr || runtime_bridge_->close_app == nullptr) {
    Serial.println("[UI_AMIGA] APP_CLOSE_FAIL reason=bridge_unavailable");
    return false;
  }
  return runtime_bridge_->close_app(reason);
}

AppRuntimeStatus AmigaUIShell::currentStatus() const {
  AppRuntimeStatus status = {};
  if (runtime_bridge_ == nullptr || runtime_bridge_->current_status == nullptr) {
    return status;
  }
  return runtime_bridge_->current_status();
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
  if (selected_index_ >= apps_.size()) {
    return;
  }

  const uint32_t now_ms = millis();
  if ((now_ms - last_launch_ms_) < LAUNCH_DEBOUNCE_MS) {
    Serial.printf("[UI_AMIGA] APP_OPEN_SKIP reason=debounce delta_ms=%lu\n",
                  static_cast<unsigned long>(now_ms - last_launch_ms_));
    return;
  }

  const AppIcon& app = apps_[selected_index_];
  const AppRuntimeStatus status = currentStatus();
  if ((status.state == AppRuntimeState::kStarting || status.state == AppRuntimeState::kRunning) &&
      std::strcmp(status.id, app.app_id.c_str()) == 0) {
    Serial.printf("[UI_AMIGA] APP_OPEN_SKIP reason=already_%s id=%s\n",
                  appRuntimeStateLabel(status.state),
                  app.app_id.c_str());
    return;
  }

  Serial.printf("[UI_AMIGA] Launching: %s\n", app.app_id.c_str());
  playTransitionFX();

  last_launch_ms_ = now_ms;
  const bool ok = requestOpenApp(app.app_id.c_str(), "default", "amiga_shell");
  if (ok) {
    Serial.printf("[UI_AMIGA] APP_OPEN_OK id=%s\n", app.app_id.c_str());
  } else {
    const AppRuntimeStatus after = currentStatus();
    Serial.printf("[UI_AMIGA] APP_OPEN_FAIL id=%s err=%s state=%s\n",
                  app.app_id.c_str(),
                  after.last_error,
                  appRuntimeStateLabel(after.state));
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
  // Touch emulation mode: buttons control virtual cursor
  if (enable_touch_emulation_) {
    uint16_t cursor_x = 0, cursor_y = 0;
    uint8_t grid_idx = 0;

    switch (button_id) {
      case 0: {  // UP - move cursor up
        touch_emulator_.moveUp();
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selected_index_ = grid_idx;
        Serial.printf("[UI_AMIGA_EMU] UP → grid[%u] at (%u,%u)\n", grid_idx, cursor_x, cursor_y);
        break;
      }

      case 1: {  // SELECT - trigger touch at cursor position
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        Serial.printf("[UI_AMIGA_EMU] SELECT → touch at (%u,%u) grid[%u]\n", 
                      cursor_x, cursor_y, grid_idx);
        handleTouchInput(cursor_x, cursor_y);
        break;
      }

      case 2: {  // DOWN - move cursor down
        touch_emulator_.moveDown();
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selected_index_ = grid_idx;
        Serial.printf("[UI_AMIGA_EMU] DOWN → grid[%u] at (%u,%u)\n", grid_idx, cursor_x, cursor_y);
        break;
      }

      case 3: {  // MENU - close running app (unchanged)
        const AppRuntimeStatus status = currentStatus();
        if (status.state == AppRuntimeState::kStarting || status.state == AppRuntimeState::kRunning) {
          const bool ok = requestCloseApp("amiga_shell_menu");
          Serial.printf("[UI_AMIGA_EMU] APP_CLOSE_%s reason=menu\n", ok ? "OK" : "FAIL");
        } else {
          Serial.println("[UI_AMIGA_EMU] MENU button: no running app");
        }
        break;
      }

      case 4: {  // LEFT/RIGHT toggle - alternate direction
        cursor_direction_toggle_ = !cursor_direction_toggle_;
        if (cursor_direction_toggle_) {
          touch_emulator_.moveRight();
          Serial.print("[UI_AMIGA_EMU] Button 4 → RIGHT ");
        } else {
          touch_emulator_.moveLeft();
          Serial.print("[UI_AMIGA_EMU] Button 4 → LEFT ");
        }
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selected_index_ = grid_idx;
        Serial.printf("→ grid[%u] at (%u,%u)\n", grid_idx, cursor_x, cursor_y);
        break;
      }

      default:
        Serial.printf("[UI_AMIGA_EMU] Unknown button: %u\n", button_id);
        break;
    }
    return;  // Exit early in emulation mode
  }

  // === STANDARD BUTTON NAVIGATION MODE ===
  // Button mapping for grid navigation:
  // Button 0 (UP):     Move selection up (previous row)
  // Button 1 (SELECT): Launch selected app
  // Button 2 (DOWN):   Move selection down (next row)
  // Button 3 (MENU):   Return to launcher or show menu
  
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
      const AppRuntimeStatus status = currentStatus();
      if (status.state == AppRuntimeState::kStarting || status.state == AppRuntimeState::kRunning) {
        const bool ok = requestCloseApp("amiga_shell_menu");
        Serial.printf("[UI_AMIGA] APP_CLOSE_%s reason=menu\n", ok ? "OK" : "FAIL");
      } else {
        Serial.println("[UI_AMIGA] MENU button: no running app");
      }
      break;
    }
    
    default:
      Serial.printf("[UI_AMIGA] Unknown button: %u\n", button_id);
      break;
  }
}
