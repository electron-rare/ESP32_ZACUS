// touch_emulator.h - Virtual touch simulation via button navigation
#pragma once

#include <Arduino.h>

// TouchEmulator simulates a touchscreen using button input.
// Maintains a virtual cursor (x, y) that snaps to a 4x4 app grid.
// Button navigation moves the cursor, SELECT button triggers touch event.
class TouchEmulator {
 public:
  TouchEmulator() = default;
  ~TouchEmulator() = default;

  // Initialize emulator with grid layout parameters.
  // @param grid_cols Number of columns in grid (default 4)
  // @param grid_rows Number of rows in grid (default 4)
  // @param cell_width Width of each grid cell in pixels (default 80)
  // @param cell_height Height of each grid cell in pixels (default 80)
  // @param offset_x Horizontal offset of grid start (default 16)
  // @param offset_y Vertical offset of grid start (default 32)
  void begin(uint8_t grid_cols = 4, uint8_t grid_rows = 4,
             uint16_t cell_width = 80, uint16_t cell_height = 80,
             uint16_t offset_x = 16, uint16_t offset_y = 32);

  // Move virtual cursor up one grid cell.
  void moveUp();

  // Move virtual cursor down one grid cell.
  void moveDown();

  // Move virtual cursor left one grid cell.
  void moveLeft();

  // Move virtual cursor right one grid cell.
  void moveRight();

  // Get current cursor position in screen coordinates (center of grid cell).
  // @param out_x Output parameter for X coordinate
  // @param out_y Output parameter for Y coordinate
  // @param out_grid_index Output parameter for grid index (0-15)
  void getCursorPosition(uint16_t* out_x, uint16_t* out_y, uint8_t* out_grid_index) const;

  // Get current grid index (0 to grid_cols*grid_rows-1).
  uint8_t getCurrentGridIndex() const { return cursor_grid_index_; }

  // Set cursor to specific grid index.
  void setGridIndex(uint8_t index);

  // Check if cursor is at valid grid position with an app.
  // @param max_apps Total number of apps available
  bool isValidPosition(uint8_t max_apps) const;

 private:
  void updateCursorCoordinates();

  uint8_t grid_cols_ = 4;
  uint8_t grid_rows_ = 4;
  uint16_t cell_width_ = 80;
  uint16_t cell_height_ = 80;
  uint16_t offset_x_ = 16;
  uint16_t offset_y_ = 32;

  uint8_t cursor_grid_index_ = 0;  // Current grid position (0-15)
  uint16_t cursor_x_ = 0;          // Screen X coordinate (center of cell)
  uint16_t cursor_y_ = 0;          // Screen Y coordinate (center of cell)
};
