## Phase 9: Touch Input & App Launcher Integration

**Date**: 2 Mars 2026
**Status**: 🚀 READY FOR EXECUTION

### Validation Results

✅ **Device Responsiveness**: PING/PONG working, STATUS complete
✅ **Firmware Stability**: No memory leaks, commands responsive
✅ **AmigaUIShell Boot**: Initialized in main.cpp setup()
✅ **System Ready**: All P1/P2 security features active

---

### Phase 9 Scope

#### 1. Touch Input Mapping (HIGH PRIORITY)

**Current State**:
- AmigaUIShell::selectApp(0-7) method defined
- Grid layout: 4x4 = 16 possible positions, using 7 apps
- Touch manager exists (g_touch) with coordinate input

**Implementation**:
```cpp
// Map touch coordinates to grid index
uint8_t grid_index = calculateGridIndex(touch_x, touch_y);
if (grid_index < 7) {
  g_amiga_shell.selectApp(grid_index);  // Select app (highlight)
  // Visual feedback: pulse effect
}
```

**Grid Layout** (assuming 320x200 display):
```
┌──────┬──────┬──────┬──────┐  (0,0)→(64,64)
│  [0] │  [1] │  [2] │  [3] │  Audio, Calc, Timer, Light
├──────┼──────┼──────┼──────┤  (0,80)→(64,144)
│  [4] │  [5] │  [6] │  --- │  Camera, Dict, QR, ---
├──────┼──────┼──────┼──────┤
│      │      │      │      │
└──────┴──────┴──────┴──────┘
```

**Coordinate Calculation**:
```cpp
uint8_t AmigaUIShell::getTouchGridIndex(uint16_t x, uint16_t y) {
  uint8_t col = x / (ICON_SIZE + ICON_SPACING);  // 64 + 16 = 80px per cell
  uint8_t row = y / (ICON_SIZE + ICON_SPACING);
  
  if (col >= GRID_COLS || row >= GRID_ROWS) return 255;  // Out of bounds
  
  uint8_t index = row * GRID_COLS + col;
  return (index < 7) ? index : 255;  // Only 7 apps available
}
```

#### 2. Button Integration (MEDIUM PRIORITY)

**Current State**:
- ButtonManager::readButtons() returns button states
- 4 buttons available (from RC_FINAL_BOARD.md)

**Mapping**:
```
Button 0 (UP):      Move selection up (previous row)
Button 1 (SELECT):  Launch selected app
Button 2 (DOWN):    Move selection down (next row)
Button 3 (MENU):    Return to launcher (if in app)
```

**Implementation**:
```cpp
void handleButtonPress(uint8_t button_id) {
  if (button_id == BUTTON_UP) {
    uint8_t new_index = (g_amiga_shell.selected_index_ >= 4) 
                        ? g_amiga_shell.selected_index_ - 4 
                        : g_amiga_shell.selected_index_;
    g_amiga_shell.selectApp(new_index);
  }
  else if (button_id == BUTTON_SELECT) {
    g_amiga_shell.launchSelectedApp();
  }
  // ... etc
}
```

#### 3. App Launch Mechanism (HIGH PRIORITY)

**Current State**:
- launchSelectedApp() defined but empty
- App registry exists with 4 core apps
- AppCoordinator arch exists (from spec)

**Implementation**:
```cpp
void AmigaUIShell::launchSelectedApp() {
  if (selected_index_ >= 7) return;
  
  const AppIcon& app = APPS[selected_index_];
  Serial.printf("[UI_AMIGA] Launching app: %s\n", app.app_id);
  
  // Transition effect (fade out)
  playTransitionFX();
  
  // TODO: Route to AppCoordinator
  // dispatch AppAction::LAUNCH to app identified by app.app_id
  // AppCoordinator::launchApp(app.app_id, context);
  // Switch UI mode from LAUNCHER to APP_RUNNING
}
```

#### 4. Return-to-Launcher Flow (MEDIUM PRIORITY)

**Mechanism**:
- App finish → onStop() called
- AppCoordinator signals launcher
- AmigaUIShell::onStart() called again
- Grid redraws with previous selection preserved

---

### Implementation Checklist

**Phase 9A: Touch/Button Input (Immediate)**
- [ ] Implement getTouchGridIndex() in ui_amiga_shell.cpp
- [ ] Add onTouchEvent() handler
- [ ] Add handleButtonPress() for grid navigation
- [ ] Test: Tap on grid → see selection highlight
- [ ] Test: Button UP/DOWN → selection moves

**Phase 9B: App Launch (Urgent)**
- [ ] Implement launchSelectedApp() logic
- [ ] Route to AppCoordinator (when available)
- [ ] Implement return-to-launcher flow
- [ ] Test: Select app → launch and run
- [ ] Test: App stop → return to launcher

**Phase 9C: Visual Polish (Nice-to-have)**
- [ ] Smooth animation transitions
- [ ] Delayed app launch (allow fade-out to complete)
- [ ] Selection wraparound (grid navigation loops)
- [ ] Long-press to see app info (future)

---

### Known Issues & Workarounds

**Issue**: 2 DALL-E icons still missing (audio_player, timer)
**Workaround**: Emoji fallbacks (🎵, ⏱️) render gracefully
**Next**: Retry DALL-E generation once quota resets

**Issue**: AppCoordinator integration pending
**Status**: Phase 9B blocked until AppCoordinator available
**Fallback**: Can test launcher UI in isolation first

---

### Testing Strategy

**Unit Tests**:
1. `getTouchGridIndex(x, y)` → correct grid_index
2. `selectApp(index)` → selected_index_ updated, pulse effect triggered
3. `launchSelectedApp()` → transition FX played + app launch signal sent

**Integration Tests**:
1. Touch grid → app selection flowmap
2. Button navigation → grid highlight moves
3. App launch → transition → app runs → return to launcher

**Hardware Tests**:
1. On live device: Tap/touch grid positions
2. Button presses: UP/DOWN/SELECT navigation
3. App launch: Start → run → stop → back to launcher

---

### Questions for Implementation

1. **Touch resolution**: Is touch input (x, y) available in UiManager?
2. **AppCoordinator**: Does it exist? Can we call it from AmigaUIShell?
3. **App registry**: Are app_id strings correct? (e.g., "audio_player" vs "app_audio")
4. **Display size**: Is display 320x200? Need to confirm grid offsets

---

### Dependencies

- ✅ AmigaUIShell class (Phase 8)
- ✅ UiManager with touch support (existing)
- ✅ ButtonManager (existing)
- ❓ AppCoordinator (Phase 9B requires)
- ❓ App lifecycle integration (Phase 9B requires)

---

### Success Criteria

✅ **Phase 9 Complete When**:
1. Touch coordinates map to grid positions ✓
2. Grid selection updates visually on input ✓
3. Button navigation works (UP/DOWN/SELECT) ✓
4. App launch transitions smoothly ✓
5. Return-to-launcher flow functional ✓
6. No memory leaks or crashes after 10 launches ✓

---

### Timeline

- Phase 9A (Touch/Button): 1-2 hours
- Phase 9B (App Launch): 2-3 hours
- Phase 9C (Polish): 1 hour
- Total Phase 9: ~4-6 hours

**Estimate complete by**: 2-3 hours from now (if proceeding immediately)

