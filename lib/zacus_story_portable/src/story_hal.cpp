#include "zacus_story_portable/story_hal.h"

namespace zacus_hal {

static StoryHAL g_hal = {};

void setHAL(const StoryHAL& hal) {
  g_hal = hal;
}

const StoryHAL& getHAL() {
  return g_hal;
}

}  // namespace zacus_hal
