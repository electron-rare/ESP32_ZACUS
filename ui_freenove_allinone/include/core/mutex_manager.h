// mutex_manager.h - Thread-safe mutex management (OO singleton, atomic stats).
// Strategy: 2 separate mutexes (audio, scenario) for fine-grained protection.
// Lock ordering: ALWAYS audio → scenario to prevent deadlock.
// Overhead: ~50µs per lock/unlock on ESP32-S3 @ 240MHz.
#pragma once

#include <atomic>
#include <cstdint>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
// MutexManager — singleton class with atomic stats
// Call sites unchanged: MutexManager::init(), MutexManager::takeAudioMutex()…
// ============================================================================

class MutexManager {
 public:
  // Meyer's singleton — thread-safe, zero-overhead after first call.
  static MutexManager& instance();

  // --- Lifecycle (call once in setup / shutdown) ---
  bool doInit();
  void doDeinit();

  // --- Low-level acquisition (prefer RAII guards below) ---
  bool takeAudio(uint32_t timeout_ms);
  void releaseAudio();
  bool takeScenario(uint32_t timeout_ms);
  void releaseScenario();
  bool takeBoth(uint32_t timeout_ms);
  void releaseBoth();

  // --- Atomic stats ---
  uint32_t audioLockCount()     const { return audio_lock_count_.load(); }
  uint32_t scenarioLockCount()  const { return scenario_lock_count_.load(); }
  uint32_t audioTimeoutCount()  const { return audio_timeout_count_.load(); }
  uint32_t scenarioTimeoutCount() const { return scenario_timeout_count_.load(); }
  uint32_t maxAudioWaitUs()     const { return max_audio_wait_us_.load(); }
  uint32_t maxScenarioWaitUs()  const { return max_scenario_wait_us_.load(); }

  // --- Static wrappers — backward-compatible with old namespace API ---
  static bool init()                              { return instance().doInit(); }
  static void deinit()                            { instance().doDeinit(); }
  static bool takeAudioMutex(uint32_t ms)         { return instance().takeAudio(ms); }
  static void releaseAudioMutex()                 { instance().releaseAudio(); }
  static bool takeScenarioMutex(uint32_t ms)      { return instance().takeScenario(ms); }
  static void releaseScenarioMutex()              { instance().releaseScenario(); }
  static bool takeBothMutexes(uint32_t ms)        { return instance().takeBoth(ms); }
  static void releaseBothMutexes()                { instance().releaseBoth(); }
  static uint32_t getAudioLockCount()             { return instance().audioLockCount(); }
  static uint32_t getScenarioLockCount()          { return instance().scenarioLockCount(); }
  static uint32_t getAudioTimeoutCount()          { return instance().audioTimeoutCount(); }
  static uint32_t getScenarioTimeoutCount()       { return instance().scenarioTimeoutCount(); }
  static uint32_t getMaxAudioWaitUs()             { return instance().maxAudioWaitUs(); }
  static uint32_t getMaxScenarioWaitUs()          { return instance().maxScenarioWaitUs(); }

  // Non-copyable, non-movable.
  MutexManager(const MutexManager&) = delete;
  MutexManager& operator=(const MutexManager&) = delete;

 private:
  MutexManager() = default;

  void updateMaxWait(std::atomic<uint32_t>& max_field, uint32_t elapsed_us);

  SemaphoreHandle_t audio_mutex_{nullptr};
  SemaphoreHandle_t scenario_mutex_{nullptr};

  std::atomic<uint32_t> audio_lock_count_{0U};
  std::atomic<uint32_t> scenario_lock_count_{0U};
  std::atomic<uint32_t> audio_timeout_count_{0U};
  std::atomic<uint32_t> scenario_timeout_count_{0U};
  std::atomic<uint32_t> max_audio_wait_us_{0U};
  std::atomic<uint32_t> max_scenario_wait_us_{0U};

  // Owner tracking for deadlock detection (atomic: safe cross-task reads).
  std::atomic<TaskHandle_t> audio_owner_{nullptr};
  std::atomic<TaskHandle_t> scenario_owner_{nullptr};
};

// ============================================================================
// RAII LOCK GUARDS — automatic lock/unlock on scope exit.
// Usage: { AudioLock lock; if (lock) g_audio.play(…); }
// ============================================================================

class AudioLock {
 public:
  explicit AudioLock(uint32_t timeout_ms = 1000U)
      : acquired_(MutexManager::instance().takeAudio(timeout_ms)) {
    if (!acquired_) {
      Serial.printf("[MUTEX] AudioLock FAILED (timeout %lu ms)\n",
                    static_cast<unsigned long>(timeout_ms));
    }
  }
  ~AudioLock() {
    if (acquired_) {
      MutexManager::instance().releaseAudio();
    }
  }
  AudioLock(const AudioLock&) = delete;
  AudioLock& operator=(const AudioLock&) = delete;
  explicit operator bool() const { return acquired_; }
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

class ScenarioLock {
 public:
  explicit ScenarioLock(uint32_t timeout_ms = 1000U)
      : acquired_(MutexManager::instance().takeScenario(timeout_ms)) {
    if (!acquired_) {
      Serial.printf("[MUTEX] ScenarioLock FAILED (timeout %lu ms)\n",
                    static_cast<unsigned long>(timeout_ms));
    }
  }
  ~ScenarioLock() {
    if (acquired_) {
      MutexManager::instance().releaseScenario();
    }
  }
  ScenarioLock(const ScenarioLock&) = delete;
  ScenarioLock& operator=(const ScenarioLock&) = delete;
  explicit operator bool() const { return acquired_; }
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

class DualLock {
 public:
  explicit DualLock(uint32_t timeout_ms = 1000U)
      : acquired_(MutexManager::instance().takeBoth(timeout_ms)) {
    if (!acquired_) {
      Serial.printf("[MUTEX] DualLock FAILED (timeout %lu ms)\n",
                    static_cast<unsigned long>(timeout_ms));
    }
  }
  ~DualLock() {
    if (acquired_) {
      MutexManager::instance().releaseBoth();
    }
  }
  DualLock(const DualLock&) = delete;
  DualLock& operator=(const DualLock&) = delete;
  explicit operator bool() const { return acquired_; }
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

// ============================================================================
// MACROS — quick guarded calls (unchanged API).
// ============================================================================

#define AUDIO_GUARDED_CALL(call) \
  do { \
    AudioLock _audio_lock(1000U); \
    if (!_audio_lock) { \
      Serial.println("[MUTEX] WARN: Audio lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)

#define SCENARIO_GUARDED_CALL(call) \
  do { \
    ScenarioLock _scenario_lock(1000U); \
    if (!_scenario_lock) { \
      Serial.println("[MUTEX] WARN: Scenario lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)

#define DUAL_GUARDED_CALL(call) \
  do { \
    DualLock _dual_lock(1000U); \
    if (!_dual_lock) { \
      Serial.println("[MUTEX] WARN: Dual lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)
