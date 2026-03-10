// ui_amiga_shell.cpp - Amiga UI Shell Implementation
#include "ui/ui_amiga_shell.h"
#include "hardware_manager.h"
#include "ui_manager.h"
#include "app/app_registry.h"
#include <LittleFS.h>
#include <cstring>

namespace {

lv_fs_drv_t g_lvgl_littlefs_drv;
bool g_lvgl_littlefs_registered = false;
constexpr uint8_t kInvalidGridIndex = 255U;
constexpr uint8_t kMaxGridRows = 16U;

String normalizeFsPath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return String("/");
  }
  if (path[0] == '/') {
    return String(path);
  }
  String normalized = "/";
  normalized += path;
  return normalized;
}

void* lvglLittleFsOpen(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
  (void)drv;
  const String normalized = normalizeFsPath(path);
  const char* open_mode = "r";
  if ((mode & LV_FS_MODE_WR) && (mode & LV_FS_MODE_RD)) {
    open_mode = "w+";
  } else if (mode == LV_FS_MODE_WR) {
    open_mode = "w";
  }

  File* file = new File(LittleFS.open(normalized.c_str(), open_mode));
  if (file == nullptr || !(*file)) {
    delete file;
    return nullptr;
  }
  return file;
}

lv_fs_res_t lvglLittleFsClose(lv_fs_drv_t* drv, void* file_p) {
  (void)drv;
  File* file = static_cast<File*>(file_p);
  if (file == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  file->close();
  delete file;
  return LV_FS_RES_OK;
}

lv_fs_res_t lvglLittleFsRead(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
  (void)drv;
  File* file = static_cast<File*>(file_p);
  if (file == nullptr || buf == nullptr || br == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  *br = static_cast<uint32_t>(file->read(static_cast<uint8_t*>(buf), btr));
  return LV_FS_RES_OK;
}

lv_fs_res_t lvglLittleFsSeek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
  (void)drv;
  File* file = static_cast<File*>(file_p);
  if (file == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  bool ok = false;
  switch (whence) {
    case LV_FS_SEEK_SET:
      ok = file->seek(pos, SeekSet);
      break;
    case LV_FS_SEEK_CUR:
      ok = file->seek(pos, SeekCur);
      break;
    case LV_FS_SEEK_END:
      ok = file->seek(pos, SeekEnd);
      break;
    default:
      return LV_FS_RES_INV_PARAM;
  }
  return ok ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t lvglLittleFsTell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
  (void)drv;
  File* file = static_cast<File*>(file_p);
  if (file == nullptr || pos_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  *pos_p = static_cast<uint32_t>(file->position());
  return LV_FS_RES_OK;
}

void ensureLvglLittleFsDriver() {
  if (g_lvgl_littlefs_registered) {
    return;
  }
  lv_fs_drv_init(&g_lvgl_littlefs_drv);
  g_lvgl_littlefs_drv.letter = 'L';
  g_lvgl_littlefs_drv.cache_size = 0;
  g_lvgl_littlefs_drv.open_cb = lvglLittleFsOpen;
  g_lvgl_littlefs_drv.close_cb = lvglLittleFsClose;
  g_lvgl_littlefs_drv.read_cb = lvglLittleFsRead;
  g_lvgl_littlefs_drv.seek_cb = lvglLittleFsSeek;
  g_lvgl_littlefs_drv.tell_cb = lvglLittleFsTell;
  lv_fs_drv_register(&g_lvgl_littlefs_drv);
  g_lvgl_littlefs_registered = true;
  Serial.println("[UI_AMIGA] LVGL FS driver registered: L: -> LittleFS");
}

const char* legacyIconPathForApp(const char* app_id) {
  if (app_id == nullptr) {
    return nullptr;
  }
  if (std::strcmp(app_id, "audio_player") == 0) return "/ui_amiga/icons/audio_player.png";
  if (std::strcmp(app_id, "calculator") == 0) return "/ui_amiga/icons/calculator.png";
  if (std::strcmp(app_id, "timer_tools") == 0) return "/ui_amiga/icons/timer.png";
  if (std::strcmp(app_id, "flashlight") == 0) return "/ui_amiga/icons/flashlight.png";
  if (std::strcmp(app_id, "camera_video") == 0) return "/ui_amiga/icons/camera.png";
  if (std::strcmp(app_id, "dictaphone") == 0) return "/ui_amiga/icons/dictaphone.png";
  if (std::strcmp(app_id, "qr_scanner") == 0) return "/ui_amiga/icons/qr_scanner.png";
  return nullptr;
}

uint8_t normalizeShellButtonId(uint8_t raw_button_id) {
  if (raw_button_id >= 1U && raw_button_id <= 5U) {
    return static_cast<uint8_t>(raw_button_id - 1U);
  }
  return raw_button_id;
}

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

uint8_t computeGridRows(size_t app_count, uint8_t cols) {
  if (cols == 0U || app_count == 0U) {
    return 1U;
  }
  size_t rows = (app_count + static_cast<size_t>(cols) - 1U) / static_cast<size_t>(cols);
  if (rows > static_cast<size_t>(kMaxGridRows)) {
    rows = static_cast<size_t>(kMaxGridRows);
  }
  return static_cast<uint8_t>(rows);
}

}  // namespace

AmigaUIShell g_amiga_shell;

// Constructor and Destructor
AmigaUIShell::AmigaUIShell()
    : hardware_(nullptr), ui_(nullptr), registry_(nullptr), touch_manager_(nullptr),
      runtime_bridge_(nullptr), enable_touch_emulation_(false), cursor_direction_toggle_(false),
      selected_index_(0), animation_elapsed_ms_(0), animating_(false),
      last_launch_ms_(0), transition_start_ms_(0), transition_active_(false) {
  // Empty body - all initialization done in init()
}

AmigaUIShell::~AmigaUIShell() {
  cleanupButtonsLVGL();
}

bool AmigaUIShell::init(HardwareManager* hw, UiManager* ui, AppRegistry* registry) {
  hardware_ = hw;
  ui_ = ui;
  registry_ = registry;

  ensureLvglLittleFsDriver();
  
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
    syncTouchEmulatorGrid();
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
      icon.icon_path = String(desc.icon_path);
      if (icon.icon_path.length() == 0) {
        icon.icon_path = String("/apps/") + icon.app_id + "/icon.png";
      }
      if (!LittleFS.exists(icon.icon_path.c_str())) {
        const char* legacy = legacyIconPathForApp(desc.id);
        if (legacy != nullptr && LittleFS.exists(legacy)) {
          icon.icon_path = String(legacy);
        }
      }
      icon.color = getAppColor(apps_.size());
      apps_.push_back(icon);
      
      Serial.printf("[UI_AMIGA] Loaded app: %s\n", icon.name.c_str());
    }
  }
  
  Serial.printf("[UI_AMIGA] Loaded %u enabled apps from registry\n", apps_.size());
  if (enable_touch_emulation_) {
    syncTouchEmulatorGrid();
  }
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
  
  // Create all buttons once
  if (app_buttons_.empty()) {
    initializeButtonsLVGL();
  }
  
  // Update styles for current selection
  updateButtonStyles();
}

void AmigaUIShell::onStop() {
  animating_ = false;
  transition_active_ = false;
  cleanupButtonsLVGL();  // Free LVGL resources
}

void AmigaUIShell::onTick(uint32_t dt_ms) {
  // Update animation
  if (animating_) {
    animation_elapsed_ms_ += dt_ms;
    
    // Update button styles for animation
    updateButtonStyles();
    
    // Animation complete after 300ms
    if (animation_elapsed_ms_ >= 300) {
      animating_ = false;
    }
  }
  
  // Update non-blocking transition animation
  updateTransitionAnimation();
}

void AmigaUIShell::selectApp(uint8_t grid_index) {
  if (grid_index < apps_.size()) {
    selected_index_ = grid_index;
    if (enable_touch_emulation_) {
      touch_emulator_.setGridIndex(grid_index);
    }
    animating_ = true;
    animation_elapsed_ms_ = 0;
    
    Serial.printf("[UI_AMIGA] Selected: %s (%u)\n", apps_[grid_index].name.c_str(), grid_index);
  }
}

void AmigaUIShell::launchSelectedApp() {
  if (selected_index_ >= apps_.size()) {
    return;
  }

  const uint32_t now_ms = millis();
  if ((now_ms - last_launch_ms_) < LAUNCH_DEBOUNCE_MS) {
    return;
  }

  const AppIcon& app = apps_[selected_index_];
  const AppRuntimeStatus status = currentStatus();
  if ((status.state == AppRuntimeState::kStarting || status.state == AppRuntimeState::kRunning) &&
      std::strcmp(status.id, app.app_id.c_str()) == 0) {
    return;
  }

  Serial.printf("[UI_AMIGA] Launching: %s\n", app.app_id.c_str());
  
  // Start non-blocking transition animation
  transition_start_ms_ = now_ms;
  transition_active_ = true;

  last_launch_ms_ = now_ms;
  const bool ok = requestOpenApp(app.app_id.c_str(), "default", "amiga_shell");
  if (!ok) {
    const AppRuntimeStatus after = currentStatus();
    Serial.printf("[UI_AMIGA] APP_OPEN_FAIL id=%s err=%s\n",
                  app.app_id.c_str(),
                  after.last_error);
  }
}

void AmigaUIShell::drawAppGrid() {
  // Grid is managed by LVGL buttons - nothing else to draw
}

void AmigaUIShell::initializeButtonsLVGL() {
  if (ui_ == nullptr || apps_.empty()) {
    return;
  }
  
  app_buttons_.clear();
  lv_obj_t* scr = lv_scr_act();
  uint16_t start_x = GRID_START_X;
  uint16_t start_y = GRID_START_Y;
  
  for (size_t i = 0; i < apps_.size(); i++) {
    uint16_t col = i % GRID_COLS;
    uint16_t row = i / GRID_COLS;
    uint16_t x = start_x + (col * (ICON_SIZE + ICON_SPACING));
    uint16_t y = start_y + (row * (ICON_SIZE + ICON_SPACING));
    
    const AppIcon& icon = apps_[i];
    
    // Create button
    lv_obj_t* btn = lv_btn_create(scr);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, ICON_SIZE, ICON_SIZE);
    
    // Convert RGB to LVGL color
    uint32_t raw_color = icon.color;
    uint8_t r = (raw_color >> 16) & 0xFF;
    uint8_t g = (raw_color >> 8) & 0xFF;
    uint8_t b = raw_color & 0xFF;
    lv_color_t lv_color = lv_color_make(r, g, b);
    
    // Style button
    lv_obj_set_style_bg_color(btn, lv_color, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 4, LV_PART_MAIN);
    
    // Add PNG icon when available
    if (icon.icon_path.length() > 0 && LittleFS.exists(icon.icon_path.c_str())) {
      lv_obj_t* img = lv_img_create(btn);
      String lvgl_path = String("L:") + icon.icon_path;
      lv_img_set_src(img, lvgl_path.c_str());
      lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 4);
    }

    // Add label
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, icon.name.c_str());
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -2);
    
    app_buttons_.push_back(btn);
  }
}

void AmigaUIShell::cleanupButtonsLVGL() {
  // Delete all LVGL button objects (lv_obj_del is safe on nullptr)
  for (auto btn : app_buttons_) {
    if (btn != nullptr) {
      lv_obj_del(btn);
    }
  }
  app_buttons_.clear();
  Serial.println("[UI_AMIGA] All buttons cleaned up");
}

void AmigaUIShell::updateButtonStyles() {
  for (size_t i = 0; i < app_buttons_.size(); i++) {
    bool selected = (i == selected_index_);
    lv_obj_t* btn = app_buttons_[i];
    
    // Update border for selection highlight
    if (selected) {
      lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN);
      lv_obj_set_style_border_color(btn, lv_color_make(0, 255, 255), LV_PART_MAIN);  // Cyan
    } else {
      lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
    }
  }
}

void AmigaUIShell::updateTransitionAnimation() {
  if (!transition_active_) {
    return;  // No transition in progress
  }
  
  const uint32_t elapsed = millis() - transition_start_ms_;
  if (elapsed >= TRANSITION_DURATION_MS) {
    // Transition complete
    transition_active_ = false;
    return;
  }
  
  // Non-blocking fade animation (could update opacity here if needed)
  // For now, just track progress
  float progress = static_cast<float>(elapsed) / static_cast<float>(TRANSITION_DURATION_MS);
  (void)progress;  // Unused, can be used for visual effects
}

uint8_t AmigaUIShell::effectiveGridRows() const {
  return computeGridRows(apps_.size(), GRID_COLS);
}

void AmigaUIShell::syncTouchEmulatorGrid() {
  touch_emulator_.begin(
      GRID_COLS,
      effectiveGridRows(),
      ICON_SIZE + ICON_SPACING,
      ICON_SIZE + ICON_SPACING,
      GRID_START_X,
      GRID_START_Y);
  touch_emulator_.setGridIndex(selected_index_);
}

void AmigaUIShell::closeRunningAppFromMenu() {
  const AppRuntimeStatus status = currentStatus();
  if (status.state == AppRuntimeState::kStarting || status.state == AppRuntimeState::kRunning) {
    requestCloseApp("amiga_shell_menu");
  }
}

uint8_t AmigaUIShell::getTouchGridIndex(uint16_t x, uint16_t y) {
  // Map display coordinates to grid index in the currently loaded apps catalog.
  // Rows are computed dynamically from app count.
  const uint8_t rows = effectiveGridRows();
  
  // Check if touch is within grid bounds
  uint16_t grid_end_x = GRID_START_X + (GRID_COLS * (ICON_SIZE + ICON_SPACING));
  uint16_t grid_end_y = GRID_START_Y + (rows * (ICON_SIZE + ICON_SPACING));
  
  if (x < GRID_START_X || x >= grid_end_x ||
      y < GRID_START_Y || y >= grid_end_y) {
    return kInvalidGridIndex;  // Out of bounds
  }
  
  // Calculate relative position within grid
  uint16_t rel_x = x - GRID_START_X;
  uint16_t rel_y = y - GRID_START_Y;
  
  // Determine column and row
  uint8_t col = rel_x / (ICON_SIZE + ICON_SPACING);
  uint8_t row = rel_y / (ICON_SIZE + ICON_SPACING);
  
  // Clamp to valid grid bounds
  if (col >= GRID_COLS) col = GRID_COLS - 1;
  if (row >= rows) row = static_cast<uint8_t>(rows - 1U);
  
  uint8_t index = row * GRID_COLS + col;
  
  // Only return valid indices for loaded apps
  return (index < apps_.size()) ? index : kInvalidGridIndex;
}

void AmigaUIShell::handleTouchInput(uint16_t x, uint16_t y) {
  uint8_t grid_index = getTouchGridIndex(x, y);
  
  if (grid_index < apps_.size()) {
    selectApp(grid_index);
    // Auto-launch on tap
    launchSelectedApp();
  }
}

void AmigaUIShell::handleButtonInput(uint8_t button_id) {
  const uint8_t normalized_button_id = normalizeShellButtonId(button_id);

  // Touch emulation mode: buttons control virtual cursor
  if (enable_touch_emulation_) {
    uint16_t cursor_x = 0, cursor_y = 0;
    uint8_t grid_idx = 0;

    switch (normalized_button_id) {
      case 0: {  // UP - move cursor up
        touch_emulator_.moveUp();
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selectApp(grid_idx);
        break;
      }

      case 1: {  // SELECT - trigger touch at cursor position
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selectApp(grid_idx);
        launchSelectedApp();
        break;
      }

      case 2: {  // DOWN - move cursor down
        touch_emulator_.moveDown();
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selectApp(grid_idx);
        break;
      }

      case 3: {  // MENU - close running app
        closeRunningAppFromMenu();
        break;
      }

      case 4: {  // LEFT/RIGHT toggle - alternate direction
        cursor_direction_toggle_ = !cursor_direction_toggle_;
        if (cursor_direction_toggle_) {
          touch_emulator_.moveRight();
        } else {
          touch_emulator_.moveLeft();
        }
        touch_emulator_.getCursorPosition(&cursor_x, &cursor_y, &grid_idx);
        selectApp(grid_idx);
        break;
      }

      default:
        break;
    }
    return;  // Exit early in emulation mode
  }

  // === STANDARD BUTTON NAVIGATION MODE ===
  switch (normalized_button_id) {
    case 0: {  // UP button - move to previous row
      if (selected_index_ >= GRID_COLS) {
        selectApp(selected_index_ - GRID_COLS);
      }
      break;
    }
    
    case 1: {  // SELECT button - launch app
      if (selected_index_ < apps_.size()) {
        launchSelectedApp();
      }
      break;
    }
    
    case 2: {  // DOWN button - move to next row
      if (selected_index_ + GRID_COLS < apps_.size()) {
        selectApp(selected_index_ + GRID_COLS);
      }
      break;
    }
    
    case 3: {  // MENU button - close running app
      closeRunningAppFromMenu();
      break;
    }
    
    case 4: {  // LEFT/RIGHT button - navigate horizontally
      uint8_t col = selected_index_ % GRID_COLS;
      uint8_t row = selected_index_ / GRID_COLS;
      uint8_t new_col = col;
      
      // Toggle direction on each press
      cursor_direction_toggle_ = !cursor_direction_toggle_;
      if (cursor_direction_toggle_) {
        // Move right
        if (col < (GRID_COLS - 1)) {
          new_col = col + 1;
        }
      } else {
        // Move left
        if (col > 0) {
          new_col = col - 1;
        }
      }
      
      uint8_t new_index = row * GRID_COLS + new_col;
      if (new_index < apps_.size()) {
        selectApp(new_index);
      }
      break;
    }
    
    default:
      break;
  }
}
