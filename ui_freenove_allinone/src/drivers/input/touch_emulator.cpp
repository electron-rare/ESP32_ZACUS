// touch_emulator.cpp - Virtual touch simulation via button navigation
#include "drivers/input/touch_emulator.h"

void TouchEmulator::begin(uint8_t grid_cols, uint8_t grid_rows,
                           uint16_t cell_width, uint16_t cell_height,
                           uint16_t offset_x, uint16_t offset_y) {
  grid_cols_ = grid_cols;
  grid_rows_ = grid_rows;
  cell_width_ = cell_width;
  cell_height_ = cell_height;
  offset_x_ = offset_x;
  offset_y_ = offset_y;
  cursor_grid_index_ = 0;
  updateCursorCoordinates();

  Serial.printf("[TOUCH_EMU] Initialized: grid %ux%u, cell %ux%u, offset (%u,%u)\n",
                grid_cols_, grid_rows_, cell_width_, cell_height_, offset_x_, offset_y_);
}

void TouchEmulator::moveUp() {
  const uint8_t current_row = cursor_grid_index_ / grid_cols_;
  if (current_row > 0) {
    cursor_grid_index_ -= grid_cols_;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move UP → grid[%u] (%u,%u)\n", 
                  cursor_grid_index_, cursor_x_, cursor_y_);
  } else {
    Serial.printf("[TOUCH_EMU] Move UP blocked (already at top row)\n");
  }
}

void TouchEmulator::moveDown() {
  const uint8_t current_row = cursor_grid_index_ / grid_cols_;
  if (current_row < grid_rows_ - 1) {
    cursor_grid_index_ += grid_cols_;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move DOWN → grid[%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  } else {
    Serial.printf("[TOUCH_EMU] Move DOWN blocked (already at bottom row)\n");
  }
}

void TouchEmulator::moveLeft() {
  const uint8_t current_col = cursor_grid_index_ % grid_cols_;
  if (current_col > 0) {
    cursor_grid_index_--;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move LEFT → grid[%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  } else {
    // Wrap to rightmost column of same row
    cursor_grid_index_ = (cursor_grid_index_ / grid_cols_) * grid_cols_ + (grid_cols_ - 1);
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move LEFT (wrap) → grid[%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  }
}

void TouchEmulator::moveRight() {
  const uint8_t current_col = cursor_grid_index_ % grid_cols_;
  if (current_col < grid_cols_ - 1) {
    cursor_grid_index_++;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move RIGHT → grid[%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  } else {
    // Wrap to leftmost column of same row
    cursor_grid_index_ = (cursor_grid_index_ / grid_cols_) * grid_cols_;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Move RIGHT (wrap) → grid[%u] (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  }
}

void TouchEmulator::getCursorPosition(uint16_t* out_x, uint16_t* out_y, uint8_t* out_grid_index) const {
  if (out_x) {
    *out_x = cursor_x_;
  }
  if (out_y) {
    *out_y = cursor_y_;
  }
  if (out_grid_index) {
    *out_grid_index = cursor_grid_index_;
  }
}

void TouchEmulator::setGridIndex(uint8_t index) {
  const uint8_t max_index = grid_cols_ * grid_rows_ - 1;
  if (index <= max_index) {
    cursor_grid_index_ = index;
    updateCursorCoordinates();
    Serial.printf("[TOUCH_EMU] Set grid[%u] → (%u,%u)\n",
                  cursor_grid_index_, cursor_x_, cursor_y_);
  } else {
    Serial.printf("[TOUCH_EMU] Invalid grid index %u (max %u)\n", index, max_index);
  }
}

bool TouchEmulator::isValidPosition(uint8_t max_apps) const {
  return cursor_grid_index_ < max_apps;
}

void TouchEmulator::updateCursorCoordinates() {
  const uint8_t row = cursor_grid_index_ / grid_cols_;
  const uint8_t col = cursor_grid_index_ % grid_cols_;

  // Calculate center of grid cell
  cursor_x_ = offset_x_ + (col * cell_width_) + (cell_width_ / 2);
  cursor_y_ = offset_y_ + (row * cell_height_) + (cell_height_ / 2);
}
