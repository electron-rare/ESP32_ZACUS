// default_scenario_v2.cpp - minimal built-in scenario fallback.
#include "scenarios/default_scenario_v2.h"

#include <cstring>

namespace {

const char* kDefaultActions[1] = {"ACTION_TRACE_STEP"};
TransitionDef kBootTransitions[1];
TransitionDef kLockedTransitions[2];
StepDef kDefaultSteps[3];
ScenarioDef kDefaultScenario;
const ScenarioDef* kScenarioCatalog[1];
bool g_initialized = false;

bool equalsText(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  return std::strcmp(lhs, rhs) == 0;
}

void initScenarioCatalog() {
  if (g_initialized) {
    return;
  }

  kBootTransitions[0].id = "TR_BOOT_TO_LOCKED";
  kBootTransitions[0].trigger = StoryTransitionTrigger::kOnEvent;
  kBootTransitions[0].eventType = StoryEventType::kButton;
  kBootTransitions[0].eventName = "ANY";
  kBootTransitions[0].targetStepId = "STEP_LOCKED";
  kBootTransitions[0].priority = 100U;
  kBootTransitions[0].afterMs = 0U;
  kBootTransitions[0].debugOnly = false;

  kLockedTransitions[0].id = "TR_LOCKED_TO_READY";
  kLockedTransitions[0].trigger = StoryTransitionTrigger::kOnEvent;
  kLockedTransitions[0].eventType = StoryEventType::kUnlock;
  kLockedTransitions[0].eventName = "UNLOCK";
  kLockedTransitions[0].targetStepId = "STEP_READY";
  kLockedTransitions[0].priority = 120U;
  kLockedTransitions[0].afterMs = 0U;
  kLockedTransitions[0].debugOnly = false;

  kLockedTransitions[1].id = "TR_LOCKED_TO_READY_BTN";
  kLockedTransitions[1].trigger = StoryTransitionTrigger::kOnEvent;
  kLockedTransitions[1].eventType = StoryEventType::kSerial;
  kLockedTransitions[1].eventName = "BTN_NEXT";
  kLockedTransitions[1].targetStepId = "STEP_READY";
  kLockedTransitions[1].priority = 110U;
  kLockedTransitions[1].afterMs = 0U;
  kLockedTransitions[1].debugOnly = false;

  kDefaultSteps[0].id = "STEP_BOOT";
  kDefaultSteps[0].resources.screenSceneId = "SCENE_CREDITS";
  kDefaultSteps[0].resources.audioPackId = "PACK_BOOT_RADIO";
  kDefaultSteps[0].resources.actionIds = kDefaultActions;
  kDefaultSteps[0].resources.actionCount = 1U;
  kDefaultSteps[0].mp3GateOpen = false;
  kDefaultSteps[0].transitions = kBootTransitions;
  kDefaultSteps[0].transitionCount = 1U;

  kDefaultSteps[1].id = "STEP_LOCKED";
  kDefaultSteps[1].resources.screenSceneId = "SCENE_LOCKED";
  kDefaultSteps[1].resources.audioPackId = "";
  kDefaultSteps[1].resources.actionIds = kDefaultActions;
  kDefaultSteps[1].resources.actionCount = 1U;
  kDefaultSteps[1].mp3GateOpen = false;
  kDefaultSteps[1].transitions = kLockedTransitions;
  kDefaultSteps[1].transitionCount = 2U;

  kDefaultSteps[2].id = "STEP_READY";
  kDefaultSteps[2].resources.screenSceneId = "SCENE_READY";
  kDefaultSteps[2].resources.audioPackId = "PACK_WIN";
  kDefaultSteps[2].resources.actionIds = kDefaultActions;
  kDefaultSteps[2].resources.actionCount = 1U;
  kDefaultSteps[2].mp3GateOpen = true;
  kDefaultSteps[2].transitions = nullptr;
  kDefaultSteps[2].transitionCount = 0U;

  kDefaultScenario.id = "DEFAULT";
  kDefaultScenario.version = 2U;
  kDefaultScenario.steps = kDefaultSteps;
  kDefaultScenario.stepCount = 3U;
  kDefaultScenario.initialStepId = "STEP_BOOT";

  kScenarioCatalog[0] = &kDefaultScenario;
  g_initialized = true;
}

}  // namespace

const ScenarioDef* storyScenarioV2Default() {
  initScenarioCatalog();
  return &kDefaultScenario;
}

const ScenarioDef* storyScenarioV2ById(const char* scenario_id) {
  initScenarioCatalog();
  if (scenario_id == nullptr || scenario_id[0] == '\0') {
    return nullptr;
  }
  for (const ScenarioDef* scenario : kScenarioCatalog) {
    if (scenario == nullptr || scenario->id == nullptr) {
      continue;
    }
    if (equalsText(scenario->id, scenario_id)) {
      return scenario;
    }
  }
  return nullptr;
}

int8_t storyFindStepIndex(const ScenarioDef& scenario, const char* step_id) {
  if (step_id == nullptr || step_id[0] == '\0' || scenario.steps == nullptr) {
    return -1;
  }
  for (uint8_t index = 0U; index < scenario.stepCount; ++index) {
    const StepDef& step = scenario.steps[index];
    if (step.id != nullptr && equalsText(step.id, step_id)) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

bool storyValidateScenarioDef(const ScenarioDef& scenario, String* out_error) {
  if (out_error != nullptr) {
    out_error->remove(0);
  }
  if (scenario.id == nullptr || scenario.id[0] == '\0') {
    if (out_error != nullptr) {
      *out_error = "missing_id";
    }
    return false;
  }
  if (scenario.steps == nullptr || scenario.stepCount == 0U) {
    if (out_error != nullptr) {
      *out_error = "missing_steps";
    }
    return false;
  }
  return true;
}

const char* storyNormalizeScreenSceneId(const char* scene_id) {
  if (scene_id == nullptr || scene_id[0] == '\0') {
    return "SCENE_READY";
  }
  return scene_id;
}

