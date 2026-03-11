// mutex_manager.cpp - MutexManager singleton implementation.
#include "core/mutex_manager.h"

// ============================================================================
// Singleton
// ============================================================================

MutexManager& MutexManager::instance() {
  static MutexManager inst;
  return inst;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool MutexManager::doInit() {
  if (audio_mutex_ != nullptr || scenario_mutex_ != nullptr) {
    Serial.println("[MUTEX] WARNING: Already initialized");
    return false;
  }

  audio_mutex_ = xSemaphoreCreateMutex();
  if (audio_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Failed to create audio mutex");
    return false;
  }

  scenario_mutex_ = xSemaphoreCreateMutex();
  if (scenario_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Failed to create scenario mutex");
    vSemaphoreDelete(audio_mutex_);
    audio_mutex_ = nullptr;
    return false;
  }

  audio_lock_count_.store(0U);
  scenario_lock_count_.store(0U);
  audio_timeout_count_.store(0U);
  scenario_timeout_count_.store(0U);
  max_audio_wait_us_.store(0U);
  max_scenario_wait_us_.store(0U);
  audio_owner_.store(nullptr, std::memory_order_relaxed);
  scenario_owner_.store(nullptr, std::memory_order_relaxed);

  Serial.println("[MUTEX] Initialized: dual-mutex (audio + scenario), atomic stats");
  return true;
}

void MutexManager::doDeinit() {
  if (audio_mutex_ != nullptr) {
    vSemaphoreDelete(audio_mutex_);
    audio_mutex_ = nullptr;
  }
  if (scenario_mutex_ != nullptr) {
    vSemaphoreDelete(scenario_mutex_);
    scenario_mutex_ = nullptr;
  }
  audio_owner_.store(nullptr, std::memory_order_relaxed);
  scenario_owner_.store(nullptr, std::memory_order_relaxed);
  Serial.println("[MUTEX] Deinitialized");
}

// ============================================================================
// Internal helper — lock-free CAS update of a max field.
// ============================================================================

void MutexManager::updateMaxWait(std::atomic<uint32_t>& max_field, uint32_t elapsed_us) {
  uint32_t current = max_field.load(std::memory_order_relaxed);
  while (elapsed_us > current &&
         !max_field.compare_exchange_weak(current, elapsed_us,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
  }
}

// ============================================================================
// Audio mutex
// ============================================================================

bool MutexManager::takeAudio(uint32_t timeout_ms) {
  if (audio_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Audio mutex not initialized");
    return false;
  }

  // Deadlock prevention: audio must be acquired before scenario.
  TaskHandle_t current = xTaskGetCurrentTaskHandle();
  if (scenario_owner_.load(std::memory_order_acquire) == current) {
    Serial.println("[MUTEX] ERROR: Deadlock prevented — scenario held, cannot take audio");
    Serial.printf("[MUTEX]   Correct order: audio → scenario. Got: scenario → audio\n");
    return false;
  }

  const uint32_t start_us = micros();
  const TickType_t ticks = (timeout_ms == 0U) ? 0U : pdMS_TO_TICKS(timeout_ms);
  const BaseType_t result = xSemaphoreTake(audio_mutex_, ticks);
  const uint32_t elapsed_us = micros() - start_us;

  if (result == pdTRUE) {
    ++audio_lock_count_;
    audio_owner_.store(current, std::memory_order_release);
    updateMaxWait(max_audio_wait_us_, elapsed_us);
    if (elapsed_us > 5000U) {
      Serial.printf("[MUTEX] Audio lock acquired after %lu µs (contention)\n",
                    static_cast<unsigned long>(elapsed_us));
    }
    return true;
  }

  ++audio_timeout_count_;
  Serial.printf("[MUTEX] ERROR: Audio timeout after %lu ms (elapsed %lu µs)\n",
                static_cast<unsigned long>(timeout_ms),
                static_cast<unsigned long>(elapsed_us));
  return false;
}

void MutexManager::releaseAudio() {
  if (audio_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Audio mutex not initialized");
    return;
  }
  if (audio_owner_.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle()) {
    Serial.println("[MUTEX] WARNING: Audio released from non-owner task");
  }
  audio_owner_.store(nullptr, std::memory_order_release);
  xSemaphoreGive(audio_mutex_);
}

// ============================================================================
// Scenario mutex
// ============================================================================

bool MutexManager::takeScenario(uint32_t timeout_ms) {
  if (scenario_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Scenario mutex not initialized");
    return false;
  }

  const uint32_t start_us = micros();
  const TickType_t ticks = (timeout_ms == 0U) ? 0U : pdMS_TO_TICKS(timeout_ms);
  const BaseType_t result = xSemaphoreTake(scenario_mutex_, ticks);
  const uint32_t elapsed_us = micros() - start_us;

  if (result == pdTRUE) {
    ++scenario_lock_count_;
    scenario_owner_.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
    updateMaxWait(max_scenario_wait_us_, elapsed_us);
    if (elapsed_us > 5000U) {
      Serial.printf("[MUTEX] Scenario lock acquired after %lu µs (contention)\n",
                    static_cast<unsigned long>(elapsed_us));
    }
    return true;
  }

  ++scenario_timeout_count_;
  Serial.printf("[MUTEX] ERROR: Scenario timeout after %lu ms (elapsed %lu µs)\n",
                static_cast<unsigned long>(timeout_ms),
                static_cast<unsigned long>(elapsed_us));
  return false;
}

void MutexManager::releaseScenario() {
  if (scenario_mutex_ == nullptr) {
    Serial.println("[MUTEX] ERROR: Scenario mutex not initialized");
    return;
  }
  if (scenario_owner_.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle()) {
    Serial.println("[MUTEX] WARNING: Scenario released from non-owner task");
  }
  scenario_owner_.store(nullptr, std::memory_order_release);
  xSemaphoreGive(scenario_mutex_);
}

// ============================================================================
// Both mutexes (deadlock-safe order: audio → scenario)
// ============================================================================

bool MutexManager::takeBoth(uint32_t timeout_ms) {
  if (!takeAudio(timeout_ms)) {
    return false;
  }
  if (!takeScenario(timeout_ms)) {
    releaseAudio();  // Rollback on partial failure.
    return false;
  }
  return true;
}

void MutexManager::releaseBoth() {
  releaseScenario();  // Reverse order: scenario → audio.
  releaseAudio();
}
