// mutex_manager.h - Thread-safe access protection for global AudioManager and ScenarioManager
// CRITICAL: Prevents race conditions between Arduino loop (core 1), WebServer (core 0), I2S callbacks
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
// MUTEX MANAGER - Dual-mutex strategy for g_audio and g_scenario protection
// Strategy: 2 separate mutexes (audio_mutex, scenario_mutex) for fine granularity
// Lock ordering: ALWAYS acquire audio_mutex before scenario_mutex to prevent deadlock
// ISR-safe: Timeout-based acquisition (max 1000ms) compatible with watchdog (30s)
// Overhead: ~50µs per lock/unlock operation on ESP32-S3 @ 240MHz
// ============================================================================

namespace MutexManager {

// Initialize mutex system (call once in setup())
// Returns true if initialization successful, false on failure
bool init();

// Cleanup (call on shutdown/restart)
void deinit();

// Low-level mutex acquisition (prefer RAII guards below)
// timeout_ms: Max wait time (0 = no wait, portMAX_DELAY = infinite)
// Returns true if lock acquired, false on timeout
bool takeAudioMutex(uint32_t timeout_ms);
void releaseAudioMutex();

bool takeScenarioMutex(uint32_t timeout_ms);
void releaseScenarioMutex();

// Acquire both mutexes with deadlock prevention (audio → scenario order)
// Returns true if both acquired, false if either acquisition failed
bool takeBothMutexes(uint32_t timeout_ms);
void releaseBothMutexes();

// Statistics (for debugging race conditions)
uint32_t audioLockCount();
uint32_t scenarioLockCount();
uint32_t audioTimeoutCount();
uint32_t scenarioTimeoutCount();
uint32_t maxAudioWaitUs();
uint32_t maxScenarioWaitUs();

}  // namespace MutexManager

// ============================================================================
// RAII LOCK GUARDS - Automatic lock/unlock with timeout protection
// Usage: { AudioLock lock; g_audio.play(...); } // auto-release on scope exit
// ============================================================================

class AudioLock {
 public:
  explicit AudioLock(uint32_t timeout_ms = 1000);
  ~AudioLock();
  AudioLock(const AudioLock&) = delete;
  AudioLock& operator=(const AudioLock&) = delete;
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

class ScenarioLock {
 public:
  explicit ScenarioLock(uint32_t timeout_ms = 1000);
  ~ScenarioLock();
  ScenarioLock(const ScenarioLock&) = delete;
  ScenarioLock& operator=(const ScenarioLock&) = delete;
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

class DualLock {
 public:
  explicit DualLock(uint32_t timeout_ms = 1000);
  ~DualLock();
  DualLock(const DualLock&) = delete;
  DualLock& operator=(const DualLock&) = delete;
  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

// ============================================================================
// INTEGRATION MACROS - Quick integration into existing code
// Example: AUDIO_GUARDED_CALL(g_audio.play("/music/track.mp3"))
// ============================================================================

#define AUDIO_GUARDED_CALL(call) \
  do { \
    AudioLock _audio_lock(1000); \
    if (!_audio_lock.acquired()) { \
      Serial.println("[MUTEX] WARN: Audio lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)

#define SCENARIO_GUARDED_CALL(call) \
  do { \
    ScenarioLock _scenario_lock(1000); \
    if (!_scenario_lock.acquired()) { \
      Serial.println("[MUTEX] WARN: Scenario lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)

#define DUAL_GUARDED_CALL(call) \
  do { \
    DualLock _dual_lock(1000); \
    if (!_dual_lock.acquired()) { \
      Serial.println("[MUTEX] WARN: Dual lock timeout"); \
    } else { \
      call; \
    } \
  } while (0)
