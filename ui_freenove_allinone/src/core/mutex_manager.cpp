// mutex_manager.cpp - Thread-safe access protection implementation
#include "core/mutex_manager.h"

namespace {

// Internal mutex handles
SemaphoreHandle_t g_audio_mutex = nullptr;
SemaphoreHandle_t g_scenario_mutex = nullptr;

// Statistics for debugging race conditions
uint32_t g_audio_lock_count = 0;
uint32_t g_scenario_lock_count = 0;
uint32_t g_audio_timeout_count = 0;
uint32_t g_scenario_timeout_count = 0;
uint32_t g_max_audio_wait_us = 0;
uint32_t g_max_scenario_wait_us = 0;

// Track mutex holder task for deadlock detection
TaskHandle_t g_audio_mutex_owner = nullptr;
TaskHandle_t g_scenario_mutex_owner = nullptr;

}  // namespace

namespace MutexManager {

bool init() {
  if (g_audio_mutex != nullptr || g_scenario_mutex != nullptr) {
    Serial.println("[MUTEX] WARNING: Already initialized");
    return false;
  }

  g_audio_mutex = xSemaphoreCreateMutex();
  if (g_audio_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Failed to create audio mutex");
    return false;
  }

  g_scenario_mutex = xSemaphoreCreateMutex();
  if (g_scenario_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Failed to create scenario mutex");
    vSemaphoreDelete(g_audio_mutex);
    g_audio_mutex = nullptr;
    return false;
  }

  // Reset stats
  g_audio_lock_count = 0;
  g_scenario_lock_count = 0;
  g_audio_timeout_count = 0;
  g_scenario_timeout_count = 0;
  g_max_audio_wait_us = 0;
  g_max_scenario_wait_us = 0;
  g_audio_mutex_owner = nullptr;
  g_scenario_mutex_owner = nullptr;

  Serial.println("[MUTEX] Initialized: dual-mutex strategy (audio + scenario)");
  return true;
}

void deinit() {
  if (g_audio_mutex != nullptr) {
    vSemaphoreDelete(g_audio_mutex);
    g_audio_mutex = nullptr;
  }
  if (g_scenario_mutex != nullptr) {
    vSemaphoreDelete(g_scenario_mutex);
    g_scenario_mutex = nullptr;
  }
  g_audio_mutex_owner = nullptr;
  g_scenario_mutex_owner = nullptr;
  Serial.println("[MUTEX] Deinitialized");
}

bool takeAudioMutex(uint32_t timeout_ms) {
  if (g_audio_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Audio mutex not initialized");
    return false;
  }

  // Deadlock detection: check if current task already owns scenario mutex
  TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
  if (g_scenario_mutex_owner == current_task) {
    Serial.println("[MUTEX] ERROR: Deadlock prevented - scenario mutex already held, cannot acquire audio mutex");
    Serial.printf("[MUTEX]   Correct order: audio → scenario. Current: scenario → audio\n");
    return false;
  }

  const uint32_t start_us = micros();
  const TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  const BaseType_t result = xSemaphoreTake(g_audio_mutex, ticks);
  const uint32_t elapsed_us = micros() - start_us;

  if (result == pdTRUE) {
    g_audio_lock_count++;
    g_audio_mutex_owner = current_task;
    if (elapsed_us > g_max_audio_wait_us) {
      g_max_audio_wait_us = elapsed_us;
    }
    if (elapsed_us > 5000) {  // Log if wait > 5ms
      Serial.printf("[MUTEX] Audio lock acquired after %lu µs (contention detected)\n", elapsed_us);
    }
    return true;
  }

  g_audio_timeout_count++;
  Serial.printf("[MUTEX] ERROR: Audio mutex timeout after %lu ms (elapsed %lu µs)\n",
                timeout_ms, elapsed_us);
  return false;
}

void releaseAudioMutex() {
  if (g_audio_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Audio mutex not initialized");
    return;
  }

  TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
  if (g_audio_mutex_owner != current_task) {
    Serial.println("[MUTEX] WARNING: Releasing audio mutex from different task than owner");
  }

  g_audio_mutex_owner = nullptr;
  xSemaphoreGive(g_audio_mutex);
}

bool takeScenarioMutex(uint32_t timeout_ms) {
  if (g_scenario_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Scenario mutex not initialized");
    return false;
  }

  const uint32_t start_us = micros();
  const TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  const BaseType_t result = xSemaphoreTake(g_scenario_mutex, ticks);
  const uint32_t elapsed_us = micros() - start_us;

  if (result == pdTRUE) {
    g_scenario_lock_count++;
    g_scenario_mutex_owner = xTaskGetCurrentTaskHandle();
    if (elapsed_us > g_max_scenario_wait_us) {
      g_max_scenario_wait_us = elapsed_us;
    }
    if (elapsed_us > 5000) {  // Log if wait > 5ms
      Serial.printf("[MUTEX] Scenario lock acquired after %lu µs (contention detected)\n", elapsed_us);
    }
    return true;
  }

  g_scenario_timeout_count++;
  Serial.printf("[MUTEX] ERROR: Scenario mutex timeout after %lu ms (elapsed %lu µs)\n",
                timeout_ms, elapsed_us);
  return false;
}

void releaseScenarioMutex() {
  if (g_scenario_mutex == nullptr) {
    Serial.println("[MUTEX] ERROR: Scenario mutex not initialized");
    return;
  }

  TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
  if (g_scenario_mutex_owner != current_task) {
    Serial.println("[MUTEX] WARNING: Releasing scenario mutex from different task than owner");
  }

  g_scenario_mutex_owner = nullptr;
  xSemaphoreGive(g_scenario_mutex);
}

bool takeBothMutexes(uint32_t timeout_ms) {
  // Acquire audio mutex first (deadlock prevention order: audio → scenario)
  if (!takeAudioMutex(timeout_ms)) {
    return false;
  }

  // Then scenario mutex
  if (!takeScenarioMutex(timeout_ms)) {
    releaseAudioMutex();  // Rollback on failure
    return false;
  }

  return true;
}

void releaseBothMutexes() {
  // Release in reverse order (scenario → audio)
  releaseScenarioMutex();
  releaseAudioMutex();
}

uint32_t audioLockCount() {
  return g_audio_lock_count;
}

uint32_t scenarioLockCount() {
  return g_scenario_lock_count;
}

uint32_t audioTimeoutCount() {
  return g_audio_timeout_count;
}

uint32_t scenarioTimeoutCount() {
  return g_scenario_timeout_count;
}

uint32_t maxAudioWaitUs() {
  return g_max_audio_wait_us;
}

uint32_t maxScenarioWaitUs() {
  return g_max_scenario_wait_us;
}

}  // namespace MutexManager

// ============================================================================
// RAII LOCK GUARDS IMPLEMENTATION
// ============================================================================

AudioLock::AudioLock(uint32_t timeout_ms)
    : acquired_(MutexManager::takeAudioMutex(timeout_ms)) {
  if (!acquired_) {
    Serial.printf("[MUTEX] AudioLock FAILED (timeout %lu ms)\n", timeout_ms);
  }
}

AudioLock::~AudioLock() {
  if (acquired_) {
    MutexManager::releaseAudioMutex();
  }
}

ScenarioLock::ScenarioLock(uint32_t timeout_ms)
    : acquired_(MutexManager::takeScenarioMutex(timeout_ms)) {
  if (!acquired_) {
    Serial.printf("[MUTEX] ScenarioLock FAILED (timeout %lu ms)\n", timeout_ms);
  }
}

ScenarioLock::~ScenarioLock() {
  if (acquired_) {
    MutexManager::releaseScenarioMutex();
  }
}

DualLock::DualLock(uint32_t timeout_ms)
    : acquired_(MutexManager::takeBothMutexes(timeout_ms)) {
  if (!acquired_) {
    Serial.printf("[MUTEX] DualLock FAILED (timeout %lu ms)\n", timeout_ms);
  }
}

DualLock::~DualLock() {
  if (acquired_) {
    MutexManager::releaseBothMutexes();
  }
}
