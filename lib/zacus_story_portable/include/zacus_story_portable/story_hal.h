#pragma once
/**
 * @file story_hal.h
 * @brief Hardware Abstraction Layer for the story engine.
 *
 * These interfaces decouple the portable story logic from
 * platform-specific APIs (Arduino, ESP-IDF, POSIX, etc.).
 * Each platform provides its own implementation.
 */

#include <stddef.h>
#include <stdint.h>

namespace zacus_hal {

/// Filesystem abstraction — replaces LittleFS / SPIFFS / VFS.
class IStoryStorage {
 public:
  virtual ~IStoryStorage() = default;

  /// Check if a file exists at the given path.
  virtual bool exists(const char* path) = 0;

  /// Read entire file into buffer. Returns bytes read, 0 on failure.
  virtual size_t readFile(const char* path, uint8_t* buf, size_t bufSize) = 0;

  /// Write buffer to file. Returns true on success.
  virtual bool writeFile(const char* path, const uint8_t* data, size_t len) = 0;

  /// List files in directory. Calls callback for each entry.
  /// callback(name, sizeBytes) — return false to stop iteration.
  virtual void listDir(const char* dirPath,
                       bool (*callback)(const char* name, size_t sizeBytes, void* ctx),
                       void* ctx) = 0;

  /// Filesystem info.
  virtual bool fsInfo(uint32_t* totalBytes, uint32_t* usedBytes) = 0;
};

/// Logging abstraction — replaces Serial.printf / ESP_LOGx.
class IStoryLogger {
 public:
  virtual ~IStoryLogger() = default;

  virtual void info(const char* tag, const char* fmt, ...) = 0;
  virtual void warn(const char* tag, const char* fmt, ...) = 0;
  virtual void error(const char* tag, const char* fmt, ...) = 0;
  virtual void debug(const char* tag, const char* fmt, ...) = 0;
};

/// Timer abstraction — replaces millis() / esp_timer_get_time().
class IStoryTimer {
 public:
  virtual ~IStoryTimer() = default;

  /// Current time in milliseconds since boot.
  virtual uint32_t millis() = 0;

  /// Delay (non-blocking if possible on the platform).
  virtual void delayMs(uint32_t ms) = 0;
};

/// Audio playback abstraction — replaces audio_manager.
class IStoryAudio {
 public:
  virtual ~IStoryAudio() = default;

  /// Play an audio file (path on filesystem or URL).
  virtual bool play(const char* source, uint8_t volume = 80, bool loop = false) = 0;

  /// Stop current playback.
  virtual void stop() = 0;

  /// Check if audio is currently playing.
  virtual bool isPlaying() = 0;

  /// Set master volume (0-100).
  virtual void setVolume(uint8_t volume) = 0;
};

/// Global HAL instance — set once at startup.
struct StoryHAL {
  IStoryStorage* storage = nullptr;
  IStoryLogger* logger = nullptr;
  IStoryTimer* timer = nullptr;
  IStoryAudio* audio = nullptr;
};

/// Set the global HAL. Must be called before any story engine use.
void setHAL(const StoryHAL& hal);

/// Get the global HAL.
const StoryHAL& getHAL();

}  // namespace zacus_hal
