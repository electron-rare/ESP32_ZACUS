// default_scenario_v2.h - fallback built-in scenario catalog.
#pragma once

#include "core/scenario_def.h"

const ScenarioDef* storyScenarioV2Default();
const ScenarioDef* storyScenarioV2ById(const char* scenario_id);

// Stub implementations after story engine removal
inline uint8_t storyScenarioV2Count() { return 0U; }
inline const char* storyScenarioV2IdAt(uint8_t index) { return ""; }

