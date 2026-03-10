// scenario_def.h - minimal story contracts used by runtime managers.
#pragma once

#include <Arduino.h>

#include <cstdint>

enum class StoryEventType : uint8_t {
  kNone = 0,
  kUnlock,
  kAudioDone,
  kTimer,
  kSerial,
  kButton,
  kEspNow,
  kAction,
};

enum class StoryTransitionTrigger : uint8_t {
  kOnEvent = 0,
  kAfterMs,
  kImmediate,
};

struct TransitionDef {
  const char* id = nullptr;
  StoryTransitionTrigger trigger = StoryTransitionTrigger::kOnEvent;
  StoryEventType eventType = StoryEventType::kNone;
  const char* eventName = nullptr;
  const char* targetStepId = nullptr;
  uint16_t priority = 0U;
  uint32_t afterMs = 0U;
  bool debugOnly = false;
};

struct StepResourcesDef {
  const char* screenSceneId = nullptr;
  const char* audioPackId = nullptr;
  const char* const* actionIds = nullptr;
  uint8_t actionCount = 0U;
};

struct StepDef {
  const char* id = nullptr;
  StepResourcesDef resources = {};
  bool mp3GateOpen = false;
  const TransitionDef* transitions = nullptr;
  uint8_t transitionCount = 0U;
};

struct ScenarioDef {
  const char* id = nullptr;
  uint16_t version = 2U;
  const StepDef* steps = nullptr;
  uint8_t stepCount = 0U;
  const char* initialStepId = nullptr;
};

int8_t storyFindStepIndex(const ScenarioDef& scenario, const char* step_id);
bool storyValidateScenarioDef(const ScenarioDef& scenario, String* out_error);
const char* storyNormalizeScreenSceneId(const char* scene_id);

