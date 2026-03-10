// player_ui_model.h - lightweight UI action state for keypad navigation hooks.
#pragma once

#include <Arduino.h>

#include <cstdint>

enum class UiActionSource : uint8_t {
  kNone = 0,
  kKeyShort,
  kKeyLong,
};

struct UiAction {
  UiActionSource source = UiActionSource::kNone;
  uint8_t key = 0U;
};

enum class PlayerUiPage : uint8_t {
  kHome = 0,
  kMedia,
  kTools,
  kKids,
};

struct PlayerUiSnapshot {
  PlayerUiPage page = PlayerUiPage::kHome;
  uint8_t cursor = 0U;
  uint16_t offset = 0U;
};

inline const char* playerUiPageLabel(PlayerUiPage page) {
  switch (page) {
    case PlayerUiPage::kMedia:
      return "MEDIA";
    case PlayerUiPage::kTools:
      return "TOOLS";
    case PlayerUiPage::kKids:
      return "KIDS";
    case PlayerUiPage::kHome:
    default:
      return "HOME";
  }
}

class PlayerUiModel {
 public:
  void reset() {
    dirty_ = true;
    snapshot_ = {};
  }

  void applyAction(const UiAction& action) {
    switch (action.key) {
      case 1U:
        snapshot_.page = PlayerUiPage::kHome;
        break;
      case 2U:
        snapshot_.page = PlayerUiPage::kMedia;
        break;
      case 3U:
        snapshot_.page = PlayerUiPage::kTools;
        break;
      case 4U:
        snapshot_.page = PlayerUiPage::kKids;
        break;
      case 5U:
        if (action.source == UiActionSource::kKeyLong) {
          snapshot_.offset = static_cast<uint16_t>(snapshot_.offset + 10U);
        } else {
          snapshot_.cursor = static_cast<uint8_t>((snapshot_.cursor + 1U) % 10U);
          snapshot_.offset = static_cast<uint16_t>(snapshot_.offset + 1U);
        }
        break;
      default:
        snapshot_.offset = static_cast<uint16_t>(snapshot_.offset + 1U);
        break;
    }
    dirty_ = true;
  }

  bool consumeDirty() {
    const bool was_dirty = dirty_;
    dirty_ = false;
    return was_dirty;
  }

  PlayerUiSnapshot snapshot() const {
    return snapshot_;
  }

 private:
  bool dirty_ = true;
  PlayerUiSnapshot snapshot_ = {};
};
