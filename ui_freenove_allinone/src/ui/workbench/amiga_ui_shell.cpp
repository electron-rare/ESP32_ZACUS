// amiga_ui_shell.cpp - Workbench-style app launcher grid (LVGL).
#include "ui/workbench/amiga_ui_shell.h"

#include <cstdio>
#include <cstring>

namespace ui::workbench {

// ── Amiga Workbench color palette ──
static constexpr lv_color_t kColBg         = LV_COLOR_MAKE(0x00, 0x55, 0xAA);  // Classic Amiga blue
static constexpr lv_color_t kColTopbar     = LV_COLOR_MAKE(0xFF, 0x88, 0x00);  // Orange title bar
static constexpr lv_color_t kColTile       = LV_COLOR_MAKE(0x00, 0x44, 0x88);  // Darker blue tile
static constexpr lv_color_t kColTileFocus  = LV_COLOR_MAKE(0xFF, 0x88, 0x00);  // Orange focus
static constexpr lv_color_t kColTileDisab  = LV_COLOR_MAKE(0x33, 0x33, 0x55);  // Grey-blue disabled
static constexpr lv_color_t kColTextWhite  = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
static constexpr lv_color_t kColTextBlack  = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static constexpr lv_color_t kColTextGrey   = LV_COLOR_MAKE(0x88, 0x88, 0xAA);

static constexpr uint16_t kTopbarHeight    = 24U;
static constexpr uint16_t kPageIndicatorH  = 18U;
static constexpr uint16_t kTilePadding     = 4U;
static constexpr uint16_t kTileCornerRad   = 3U;
static constexpr uint16_t kGridPadTop      = 4U;
static constexpr uint16_t kGridPadSide     = 6U;

// ── Category icon symbols (pixel-art fallback using LV symbols) ──
static const char* categoryIcon(const char* category) {
  if (category == nullptr) return LV_SYMBOL_FILE;
  if (std::strcmp(category, "media") == 0)    return LV_SYMBOL_AUDIO;
  if (std::strcmp(category, "capture") == 0)  return LV_SYMBOL_IMAGE;
  if (std::strcmp(category, "utility") == 0)  return LV_SYMBOL_SETTINGS;
  if (std::strcmp(category, "kids") == 0)     return LV_SYMBOL_HOME;
  if (std::strcmp(category, "emulator") == 0) return LV_SYMBOL_PLAY;
  return LV_SYMBOL_FILE;
}

// ── Init ──

bool AmigaUIShell::begin(const Config& cfg, AppRegistry* registry, AppRuntimeManager* runtime) {
  cfg_ = cfg;
  registry_ = registry;
  runtime_ = runtime;

  if (registry_ == nullptr) {
    Serial.println("[WB] begin failed: no registry");
    return false;
  }

  enabled_count_ = registry_->enabledCount();
  const uint8_t tiles_per_page = cfg_.grid_cols * cfg_.grid_rows;
  page_count_ = (enabled_count_ + tiles_per_page - 1U) / tiles_per_page;
  if (page_count_ == 0U) page_count_ = 1U;
  if (page_count_ > kMaxPages) page_count_ = kMaxPages;

  current_page_ = 0U;
  focus_index_ = 0U;

  // Compute tile dimensions.
  const uint16_t grid_w = cfg_.screen_w - (kGridPadSide * 2U);
  const uint16_t grid_h = cfg_.screen_h - kTopbarHeight - kPageIndicatorH - kGridPadTop;
  tile_w_ = (grid_w - (kTilePadding * (cfg_.grid_cols - 1U))) / cfg_.grid_cols;
  tile_h_ = (grid_h - (kTilePadding * (cfg_.grid_rows - 1U))) / cfg_.grid_rows;
  grid_x_offset_ = kGridPadSide;
  grid_y_offset_ = kTopbarHeight + kGridPadTop;

  if (!styles_inited_) {
    // Background.
    lv_style_init(&st_bg_);
    lv_style_set_bg_color(&st_bg_, kColBg);
    lv_style_set_bg_opa(&st_bg_, LV_OPA_COVER);
    lv_style_set_radius(&st_bg_, 0);
    lv_style_set_border_width(&st_bg_, 0);
    lv_style_set_pad_all(&st_bg_, 0);

    // Top bar.
    lv_style_init(&st_topbar_);
    lv_style_set_bg_color(&st_topbar_, kColTopbar);
    lv_style_set_bg_opa(&st_topbar_, LV_OPA_COVER);
    lv_style_set_radius(&st_topbar_, 0);
    lv_style_set_border_width(&st_topbar_, 0);
    lv_style_set_pad_left(&st_topbar_, 6);
    lv_style_set_pad_right(&st_topbar_, 6);
    lv_style_set_pad_top(&st_topbar_, 2);

    // Tile normal.
    lv_style_init(&st_tile_);
    lv_style_set_bg_color(&st_tile_, kColTile);
    lv_style_set_bg_opa(&st_tile_, LV_OPA_COVER);
    lv_style_set_radius(&st_tile_, kTileCornerRad);
    lv_style_set_border_width(&st_tile_, 1);
    lv_style_set_border_color(&st_tile_, kColTextGrey);
    lv_style_set_pad_all(&st_tile_, 2);

    // Tile focused.
    lv_style_init(&st_tile_focused_);
    lv_style_set_bg_color(&st_tile_focused_, kColTileFocus);
    lv_style_set_bg_opa(&st_tile_focused_, LV_OPA_COVER);
    lv_style_set_border_width(&st_tile_focused_, 2);
    lv_style_set_border_color(&st_tile_focused_, kColTextWhite);

    // Tile disabled.
    lv_style_init(&st_tile_disabled_);
    lv_style_set_bg_color(&st_tile_disabled_, kColTileDisab);
    lv_style_set_bg_opa(&st_tile_disabled_, LV_OPA_70);
    lv_style_set_text_color(&st_tile_disabled_, kColTextGrey);

    // Label.
    lv_style_init(&st_label_);
    lv_style_set_text_color(&st_label_, kColTextWhite);
    lv_style_set_text_font(&st_label_, &lv_font_montserrat_14);

    // Status.
    lv_style_init(&st_status_);
    lv_style_set_text_color(&st_status_, kColTextBlack);
    lv_style_set_text_font(&st_status_, &lv_font_montserrat_14);

    // Page indicator.
    lv_style_init(&st_page_indicator_);
    lv_style_set_text_color(&st_page_indicator_, kColTextWhite);
    lv_style_set_text_font(&st_page_indicator_, &lv_font_montserrat_14);

    styles_inited_ = true;
  }

  Serial.printf("[WB] begin ok apps=%u pages=%u tile=%ux%u grid=%ux%u\n",
                enabled_count_, page_count_, tile_w_, tile_h_,
                cfg_.grid_cols, cfg_.grid_rows);
  return true;
}

// ── Show / Hide ──

void AmigaUIShell::show() {
  if (visible_) return;
  if (root_ == nullptr) createUi();
  rebuildGrid();
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
  visible_ = true;
  Serial.println("[WB] show");
}

void AmigaUIShell::hide() {
  if (!visible_) return;
  if (root_ != nullptr) {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }
  visible_ = false;
  Serial.println("[WB] hide");
}

void AmigaUIShell::toggle() {
  if (visible_) hide();
  else show();
}

// ── Create LVGL UI ──

void AmigaUIShell::createUi() {
  if (root_ != nullptr) return;

  root_ = lv_obj_create(lv_layer_top());
  lv_obj_add_style(root_, &st_bg_, 0);
  lv_obj_set_size(root_, cfg_.screen_w, cfg_.screen_h);
  lv_obj_set_pos(root_, 0, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

  // ── Top bar ──
  topbar_ = lv_obj_create(root_);
  lv_obj_add_style(topbar_, &st_topbar_, 0);
  lv_obj_set_size(topbar_, cfg_.screen_w, kTopbarHeight);
  lv_obj_set_pos(topbar_, 0, 0);
  lv_obj_clear_flag(topbar_, LV_OBJ_FLAG_SCROLLABLE);

  topbar_label_ = lv_label_create(topbar_);
  lv_obj_add_style(topbar_label_, &st_status_, 0);
  lv_label_set_text(topbar_label_, "Workbench");
  lv_obj_align(topbar_label_, LV_ALIGN_LEFT_MID, 0, 0);

  topbar_status_ = lv_label_create(topbar_);
  lv_obj_add_style(topbar_status_, &st_status_, 0);
  lv_label_set_text(topbar_status_, "");
  lv_obj_align(topbar_status_, LV_ALIGN_RIGHT_MID, 0, 0);

  // ── Grid container ──
  grid_container_ = lv_obj_create(root_);
  lv_obj_set_style_bg_opa(grid_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid_container_, 0, 0);
  lv_obj_set_style_pad_all(grid_container_, 0, 0);
  lv_obj_set_size(grid_container_,
                  cfg_.screen_w - (kGridPadSide * 2U),
                  cfg_.screen_h - kTopbarHeight - kPageIndicatorH - kGridPadTop);
  lv_obj_set_pos(grid_container_, grid_x_offset_, grid_y_offset_);
  lv_obj_clear_flag(grid_container_, LV_OBJ_FLAG_SCROLLABLE);

  // ── Page indicator ──
  page_indicator_ = lv_label_create(root_);
  lv_obj_add_style(page_indicator_, &st_page_indicator_, 0);
  lv_label_set_text(page_indicator_, "");
  lv_obj_align(page_indicator_, LV_ALIGN_BOTTOM_MID, 0, -2);

  // ── Launch overlay (hidden by default) ──
  launch_overlay_ = lv_obj_create(root_);
  lv_obj_set_style_bg_color(launch_overlay_, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
  lv_obj_set_style_bg_opa(launch_overlay_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(launch_overlay_, 0, 0);
  lv_obj_set_style_radius(launch_overlay_, 8, 0);
  lv_obj_set_size(launch_overlay_, 200, 50);
  lv_obj_align(launch_overlay_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_clear_flag(launch_overlay_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(launch_overlay_, LV_OBJ_FLAG_HIDDEN);

  launch_label_ = lv_label_create(launch_overlay_);
  lv_obj_add_style(launch_label_, &st_label_, 0);
  lv_label_set_text(launch_label_, "Ouverture...");
  lv_obj_center(launch_label_);

  Serial.println("[WB] UI created");
}

void AmigaUIShell::destroyUi() {
  if (root_ != nullptr) {
    lv_obj_del(root_);
    root_ = nullptr;
    topbar_ = nullptr;
    topbar_label_ = nullptr;
    topbar_status_ = nullptr;
    grid_container_ = nullptr;
    page_indicator_ = nullptr;
    launch_overlay_ = nullptr;
    launch_label_ = nullptr;
  }
  tile_count_ = 0U;
  for (auto& t : tiles_) {
    t.container = nullptr;
    t.icon_label = nullptr;
    t.title_label = nullptr;
    t.global_app_index = 0xFFU;
  }
}

// ── Grid rebuild ──

void AmigaUIShell::rebuildGrid() {
  if (grid_container_ == nullptr || registry_ == nullptr) return;

  // Clear existing tiles.
  lv_obj_clean(grid_container_);
  tile_count_ = 0U;

  const uint8_t tiles_per_page = cfg_.grid_cols * cfg_.grid_rows;
  const uint8_t page_start = current_page_ * tiles_per_page;
  uint8_t tiles_this_page = tilesOnCurrentPage();

  for (uint8_t i = 0U; i < tiles_this_page && i < kMaxTilesPerPage; ++i) {
    const uint8_t global_idx = page_start + i;
    const AppEntry* app = registry_->enabledEntry(global_idx);
    if (app == nullptr) break;

    const uint8_t col = i % cfg_.grid_cols;
    const uint8_t row = i / cfg_.grid_cols;

    const int16_t x = static_cast<int16_t>(col * (tile_w_ + kTilePadding));
    const int16_t y = static_cast<int16_t>(row * (tile_h_ + kTilePadding));

    TileSlot& slot = tiles_[tile_count_];
    slot.global_app_index = global_idx;

    // Tile container.
    slot.container = lv_obj_create(grid_container_);
    lv_obj_add_style(slot.container, &st_tile_, 0);
    lv_obj_set_size(slot.container, tile_w_, tile_h_);
    lv_obj_set_pos(slot.container, x, y);
    lv_obj_clear_flag(slot.container, LV_OBJ_FLAG_SCROLLABLE);

    if (!app->enabled) {
      lv_obj_add_style(slot.container, &st_tile_disabled_, 0);
    }

    // Icon (symbol fallback).
    slot.icon_label = lv_label_create(slot.container);
    lv_obj_set_style_text_font(slot.icon_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(slot.icon_label, kColTextWhite, 0);
    lv_label_set_text(slot.icon_label, categoryIcon(app->category));
    lv_obj_align(slot.icon_label, LV_ALIGN_TOP_MID, 0, 4);

    // Title label.
    slot.title_label = lv_label_create(slot.container);
    lv_obj_add_style(slot.title_label, &st_label_, 0);
    lv_obj_set_style_text_font(slot.title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(slot.title_label, app->title);
    lv_label_set_long_mode(slot.title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(slot.title_label, tile_w_ - 8);
    lv_obj_set_style_text_align(slot.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(slot.title_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    ++tile_count_;
  }

  // Update page indicator.
  if (page_count_ > 1U) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u / %u", current_page_ + 1U, page_count_);
    lv_label_set_text(page_indicator_, buf);
    lv_obj_clear_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
  }

  // Clamp focus.
  if (focus_index_ >= tile_count_ && tile_count_ > 0U) {
    focus_index_ = tile_count_ - 1U;
  }

  updateFocusVisual();
}

// ── Focus visual ──

void AmigaUIShell::updateFocusVisual() {
  for (uint8_t i = 0U; i < tile_count_; ++i) {
    TileSlot& slot = tiles_[i];
    if (slot.container == nullptr) continue;

    if (i == focus_index_) {
      lv_obj_add_style(slot.container, &st_tile_focused_, 0);
    } else {
      lv_obj_remove_style(slot.container, &st_tile_focused_, 0);
      lv_obj_add_style(slot.container, &st_tile_, 0);
    }
  }
}

// ── Navigation ──

void AmigaUIShell::navigateFocus(int8_t dx, int8_t dy) {
  if (tile_count_ == 0U) return;

  const uint8_t cols = cfg_.grid_cols;
  uint8_t col = focus_index_ % cols;
  uint8_t row = focus_index_ / cols;

  if (dx > 0) {
    if (col + 1U < cols && (focus_index_ + 1U) < tile_count_) {
      ++focus_index_;
    } else {
      // Wrap to next page.
      navigatePage(1);
      return;
    }
  } else if (dx < 0) {
    if (col > 0U) {
      --focus_index_;
    } else {
      navigatePage(-1);
      return;
    }
  }

  if (dy > 0) {
    const uint8_t next = focus_index_ + cols;
    if (next < tile_count_) {
      focus_index_ = next;
    }
  } else if (dy < 0) {
    if (focus_index_ >= cols) {
      focus_index_ -= cols;
    }
  }

  updateFocusVisual();
}

void AmigaUIShell::navigatePage(int8_t delta) {
  if (page_count_ <= 1U) return;

  int8_t next_page = static_cast<int8_t>(current_page_) + delta;
  if (next_page < 0) next_page = static_cast<int8_t>(page_count_ - 1U);
  if (next_page >= static_cast<int8_t>(page_count_)) next_page = 0;

  current_page_ = static_cast<uint8_t>(next_page);
  focus_index_ = 0U;
  rebuildGrid();
}

void AmigaUIShell::handleInput(InputKey key) {
  if (!visible_) return;

  switch (key) {
    case InputKey::kUp:    navigateFocus(0, -1); break;
    case InputKey::kDown:  navigateFocus(0, 1);  break;
    case InputKey::kLeft:  navigateFocus(-1, 0); break;
    case InputKey::kRight: navigateFocus(1, 0);  break;
    case InputKey::kOk:    launchFocused(millis()); break;
    case InputKey::kBack:
      if (runtime_ != nullptr && runtime_->isRunning()) {
        requestCloseApp("back_button", millis());
      }
      break;
  }
}

void AmigaUIShell::handleButtonKey(uint8_t hw_key, bool long_press) {
  // Map physical 5-button ladder to InputKey.
  // BTN1=UP, BTN2=DOWN, BTN3=OK, BTN4=BACK, BTN5=RIGHT(page next)
  switch (hw_key) {
    case 1U: handleInput(InputKey::kUp);    break;
    case 2U: handleInput(InputKey::kDown);  break;
    case 3U:
      if (long_press) {
        handleInput(InputKey::kBack);
      } else {
        handleInput(InputKey::kOk);
      }
      break;
    case 4U: handleInput(InputKey::kBack);  break;
    case 5U: handleInput(InputKey::kRight); break;
    default: break;
  }
}

// ── Launch ──

bool AmigaUIShell::launchFocused(uint32_t now_ms) {
  const AppEntry* app = focusedEntry();
  if (app == nullptr || runtime_ == nullptr) return false;

  // Show launch overlay.
  if (launch_overlay_ != nullptr && launch_label_ != nullptr) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Ouverture %s...", app->title);
    lv_label_set_text(launch_label_, buf);
    lv_obj_clear_flag(launch_overlay_, LV_OBJ_FLAG_HIDDEN);
    launch_overlay_hide_ms_ = now_ms + 2000U;
  }

  const bool ok = runtime_->open(app->id, "default", now_ms);
  if (!ok) {
    if (launch_label_ != nullptr) {
      lv_label_set_text(launch_label_, "Erreur ouverture");
      launch_overlay_hide_ms_ = now_ms + 1500U;
    }
    Serial.printf("[WB] launch failed app=%s\n", app->id);
  } else {
    // Hide workbench, app scene will take over.
    hide();
  }
  return ok;
}

bool AmigaUIShell::requestCloseApp(const char* reason, uint32_t now_ms) {
  if (runtime_ == nullptr) return false;
  const bool ok = runtime_->close(reason, now_ms);
  if (ok) {
    show();  // Return to workbench.
  }
  return ok;
}

const AppEntry* AmigaUIShell::focusedEntry() const {
  if (tile_count_ == 0U || focus_index_ >= tile_count_) return nullptr;
  const uint8_t global_idx = tiles_[focus_index_].global_app_index;
  return registry_->enabledEntry(global_idx);
}

// ── Tick ──

void AmigaUIShell::tick(uint32_t now_ms) {
  if (!visible_) return;

  // Auto-hide launch overlay.
  if (launch_overlay_ != nullptr &&
      !lv_obj_has_flag(launch_overlay_, LV_OBJ_FLAG_HIDDEN) &&
      launch_overlay_hide_ms_ != 0U &&
      static_cast<int32_t>(now_ms - launch_overlay_hide_ms_) >= 0) {
    lv_obj_add_flag(launch_overlay_, LV_OBJ_FLAG_HIDDEN);
    launch_overlay_hide_ms_ = 0U;
  }

  // Periodic status bar update.
  if (now_ms - last_status_update_ms_ >= 1000U) {
    updateStatusBar(now_ms);
    last_status_update_ms_ = now_ms;
  }
}

void AmigaUIShell::updateStatusBar(uint32_t now_ms) {
  if (topbar_status_ == nullptr) return;

  char buf[40];
  const uint32_t uptime_s = now_ms / 1000U;
  const uint32_t m = (uptime_s / 60U) % 60U;
  const uint32_t h = uptime_s / 3600U;
  std::snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m));
  lv_label_set_text(topbar_status_, buf);
}

void AmigaUIShell::updateTileStates() {
  // Future: update tile badges based on runtime state.
}

uint8_t AmigaUIShell::tilesOnCurrentPage() const {
  const uint8_t tiles_per_page = cfg_.grid_cols * cfg_.grid_rows;
  const uint8_t page_start = current_page_ * tiles_per_page;
  if (page_start >= enabled_count_) return 0U;
  const uint8_t remaining = enabled_count_ - page_start;
  return (remaining < tiles_per_page) ? remaining : tiles_per_page;
}

uint8_t AmigaUIShell::globalIndexFromPageLocal(uint8_t local) const {
  const uint8_t tiles_per_page = cfg_.grid_cols * cfg_.grid_rows;
  return current_page_ * tiles_per_page + local;
}

}  // namespace ui::workbench
