// touch_emulator.cpp - Virtual touch simulation via button navigation.
#include "drivers/input/touch_emulator.h"

void TouchEmulator::begin(uint8_t grid_cols, uint8_t grid_rows,
                          uint16_t cell_width, uint16_t cell_height,
                          uint16_t offset_x, uint16_t offset_y) {
  grid_cols_   = grid_cols;
  grid_rows_   = grid_rows;
  cell_width_  = cell_width;
  cell_height_ = cell_height;
  offset_x_    = offset_x;
  offset_y_    = offset_y;
  cursor_grid_index_ = 0;
  updateCursorCoordinates();

  // One-time init log — always kept.
  Serial.printf("[TOUCH_EMU] grid=%ux%u cell=%ux%u offset=(%u,%u)\n",
                grid_cols_, grid_rows_, cell_width_, cell_height_,
                offset_x_, offset_y_);
}

void TouchEmulator::moveUp() {
  const uint8_t current_row = cursor_grid_index_ / grid_cols_;
  if (current_row > 0) {
    cursor_grid_index_ -= grid_cols_;
    updateCursorCoordinates();
#if ZACUS_SPRINT_DIAG_MODE
    Serial.printf("[TOUCH_EMU] UP → [%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
#endif
  }
}

void TouchEmulator::moveDown() {
  const uint8_t current_row = cursor_grid_index_ / grid_cols_;
  if (current_row < grid_rows_ - 1) {
    cursor_grid_index_ += grid_cols_;
    updateCursorCoordinates();
#if ZACUS_SPRINT_DIAG_MODE
    Serial.printf("[TOUCH_EMU] DOWN → [%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
#endif
  }
}

void TouchEmulator::moveLeft() {
  const uint8_t current_col = cursor_grid_index_ % grid_cols_;
  if (current_col > 0) {
    cursor_grid_index_--;
  } else {
    // Wrap to rightmost column of same row.
    cursor_grid_index_ = (cursor_grid_index_ / grid_cols_) * grid_cols_ + (grid_cols_ - 1);
  }
  updateCursorCoordinates();
#if ZACUS_SPRINT_DIAG_MODE
  Serial.printf("[TOUCH_EMU] LEFT → [%u] (%u,%u)\n",
                cursor_grid_index_, cursor_x_, cursor_y_);
#endif
}

void TouchEmulator::moveRight() {
  const uint8_t current_col = cursor_grid_index_ % grid_cols_;
  if (current_col < grid_cols_ - 1) {
    cursor_grid_index_++;
  } else {
    // Wrap to leftmost column of same row.
    cursor_grid_index_ = (cursor_grid_index_ / grid_cols_) * grid_cols_;
  }
  updateCursorCoordinates();
#if ZACUS_SPRINT_DIAG_MODE
  Serial.printf("[TOUCH_EMU] RIGHT → [%u] (%u,%u)\n",
                cursor_grid_index_, cursor_x_, cursor_y_);
#endif
}

void TouchEmulator::getCursorPosition(uint16_t* out_x, uint16_t* out_y,
                                      uint8_t* out_grid_index) const {
  if (out_x)          *out_x          = cursor_x_;
  if (out_y)          *out_y          = cursor_y_;
  if (out_grid_index) *out_grid_index = cursor_grid_index_;
}

void TouchEmulator::setGridIndex(uint8_t index) {
  const uint8_t max_index = static_cast<uint8_t>(grid_cols_ * grid_rows_ - 1);
  if (index <= max_index) {
    cursor_grid_index_ = index;
    updateCursorCoordinates();
#if ZACUS_SPRINT_DIAG_MODE
    Serial.printf("[TOUCH_EMU] set [%u] → (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
#endif
  } else {
    // Always log invalid index — indicates a bug.
    Serial.printf("[TOUCH_EMU] ERROR: invalid index %u (max %u)\n", index, max_index);
  }
}

bool TouchEmulator::isValidPosition(uint8_t max_apps) const {
  return cursor_grid_index_ < max_apps;
}

void TouchEmulator::updateCursorCoordinates() {
  const uint8_t row = cursor_grid_index_ / grid_cols_;
  const uint8_t col = cursor_grid_index_ % grid_cols_;
  cursor_x_ = offset_x_ + (col * cell_width_)  + (cell_width_  / 2U);
  cursor_y_ = offset_y_ + (row * cell_height_) + (cell_height_ / 2U);
}
