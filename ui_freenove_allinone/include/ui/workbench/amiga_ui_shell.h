// amiga_ui_shell.h - Workbench-style app launcher grid (LVGL).
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "app/app_registry.h"
#include "app/app_runtime_manager.h"

namespace ui::workbench {

class AmigaUIShell {
 public:
  // Display layout: portrait 320x480 => 3 cols x 5 rows = 15 tiles/page.
  // Landscape 480x320 => 4 cols x 3 rows = 12 tiles/page.
  static constexpr uint8_t kGridColsPortrait = 3U;
  static constexpr uint8_t kGridRowsPortrait = 5U;
  static constexpr uint8_t kGridColsLandscape = 4U;
  static constexpr uint8_t kGridRowsLandscape = 3U;

  static constexpr uint8_t kTilesPerPage = 12U;  // Conservative: fits both orientations.
  static constexpr uint8_t kMaxPages = 3U;
  static constexpr uint8_t kMaxVisibleTiles = kTilesPerPage * kMaxPages;

  enum class InputKey : uint8_t {
    kUp = 0,
    kDown,
    kLeft,
    kRight,
    kOk,
    kBack,
  };

  struct Config {
    bool landscape = true;  // Default: rotation=1 => landscape.
    uint16_t screen_w = 480U;
    uint16_t screen_h = 320U;
    uint8_t grid_cols = kGridColsLandscape;
    uint8_t grid_rows = kGridRowsLandscape;
  };

  AmigaUIShell() = default;
  ~AmigaUIShell() = default;
  AmigaUIShell(const AmigaUIShell&) = delete;
  AmigaUIShell& operator=(const AmigaUIShell&) = delete;

  bool begin(const Config& cfg, AppRegistry* registry, AppRuntimeManager* runtime);
  void show();
  void hide();
  bool visible() const { return visible_; }
  void toggle();

  void handleInput(InputKey key);
  void handleButtonKey(uint8_t hw_key, bool long_press);
  void tick(uint32_t now_ms);

  // Request app launch from the currently focused tile.
  bool launchFocused(uint32_t now_ms);
  bool requestCloseApp(const char* reason, uint32_t now_ms);

  uint8_t currentPage() const { return current_page_; }
  uint8_t pageCount() const { return page_count_; }
  uint8_t focusIndex() const { return focus_index_; }
  const AppEntry* focusedEntry() const;

 private:
  void createUi();
  void destroyUi();
  void rebuildGrid();
  void updateFocusVisual();
  void updateStatusBar(uint32_t now_ms);
  void updateTileStates();
  void navigatePage(int8_t delta);
  void navigateFocus(int8_t dx, int8_t dy);
  uint8_t tilesOnCurrentPage() const;
  uint8_t globalIndexFromPageLocal(uint8_t local) const;

  // Styles.
  lv_style_t st_bg_;
  lv_style_t st_topbar_;
  lv_style_t st_tile_;
  lv_style_t st_tile_focused_;
  lv_style_t st_tile_disabled_;
  lv_style_t st_label_;
  lv_style_t st_status_;
  lv_style_t st_page_indicator_;
  bool styles_inited_ = false;

  // Layout.
  Config cfg_;
  uint16_t tile_w_ = 0U;
  uint16_t tile_h_ = 0U;
  uint16_t grid_x_offset_ = 0U;
  uint16_t grid_y_offset_ = 0U;

  // Data.
  AppRegistry* registry_ = nullptr;
  AppRuntimeManager* runtime_ = nullptr;
  uint8_t enabled_count_ = 0U;
  uint8_t page_count_ = 0U;
  uint8_t current_page_ = 0U;
  uint8_t focus_index_ = 0U;  // local index on current page
  bool visible_ = false;

  // LVGL objects.
  lv_obj_t* root_ = nullptr;
  lv_obj_t* topbar_ = nullptr;
  lv_obj_t* topbar_label_ = nullptr;
  lv_obj_t* topbar_status_ = nullptr;
  lv_obj_t* grid_container_ = nullptr;
  lv_obj_t* page_indicator_ = nullptr;

  static constexpr uint8_t kMaxTilesPerPage = 15U;  // Max visible on one page.
  struct TileSlot {
    lv_obj_t* container = nullptr;
    lv_obj_t* icon_label = nullptr;
    lv_obj_t* title_label = nullptr;
    uint8_t global_app_index = 0xFFU;
  };
  TileSlot tiles_[kMaxTilesPerPage];
  uint8_t tile_count_ = 0U;

  // Launching feedback.
  lv_obj_t* launch_overlay_ = nullptr;
  lv_obj_t* launch_label_ = nullptr;
  uint32_t launch_overlay_hide_ms_ = 0U;

  uint32_t last_status_update_ms_ = 0U;
};

}  // namespace ui::workbench
