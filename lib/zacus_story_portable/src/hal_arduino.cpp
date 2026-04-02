/**
 * @file hal_arduino.cpp
 * @brief Arduino/PlatformIO implementation of the Story HAL.
 *
 * Compile this only when building with Arduino framework.
 * For ESP-IDF, use hal_espidf.cpp instead.
 */
#ifdef ARDUINO

#include "zacus_story_portable/story_hal.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <stdio.h>

namespace zacus_hal {

// --- Arduino Storage (LittleFS) ---

class ArduinoStorage final : public IStoryStorage {
 public:
  bool exists(const char* path) override {
    return LittleFS.exists(path);
  }

  size_t readFile(const char* path, uint8_t* buf, size_t bufSize) override {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    size_t read = f.read(buf, bufSize);
    f.close();
    return read;
  }

  bool writeFile(const char* path, const uint8_t* data, size_t len) override {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t written = f.write(data, len);
    f.close();
    return written == len;
  }

  void listDir(const char* dirPath,
               bool (*callback)(const char* name, size_t sizeBytes, void* ctx),
               void* ctx) override {
    File root = LittleFS.open(dirPath);
    if (!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while (file) {
      if (!callback(file.name(), file.size(), ctx)) break;
      file = root.openNextFile();
    }
  }

  bool fsInfo(uint32_t* totalBytes, uint32_t* usedBytes) override {
    if (totalBytes) *totalBytes = LittleFS.totalBytes();
    if (usedBytes) *usedBytes = LittleFS.usedBytes();
    return true;
  }
};

// --- Arduino Logger (Serial) ---

class ArduinoLogger final : public IStoryLogger {
 public:
  void info(const char* tag, const char* fmt, ...) override {
    va_list args;
    va_start(args, fmt);
    Serial.printf("[I][%s] ", tag);
    vprintf(fmt, args);
    Serial.println();
    va_end(args);
  }

  void warn(const char* tag, const char* fmt, ...) override {
    va_list args;
    va_start(args, fmt);
    Serial.printf("[W][%s] ", tag);
    vprintf(fmt, args);
    Serial.println();
    va_end(args);
  }

  void error(const char* tag, const char* fmt, ...) override {
    va_list args;
    va_start(args, fmt);
    Serial.printf("[E][%s] ", tag);
    vprintf(fmt, args);
    Serial.println();
    va_end(args);
  }

  void debug(const char* tag, const char* fmt, ...) override {
    va_list args;
    va_start(args, fmt);
    Serial.printf("[D][%s] ", tag);
    vprintf(fmt, args);
    Serial.println();
    va_end(args);
  }
};

// --- Arduino Timer (millis) ---

class ArduinoTimer final : public IStoryTimer {
 public:
  uint32_t millis() override {
    return ::millis();
  }

  void delayMs(uint32_t ms) override {
    ::delay(ms);
  }
};

// --- Arduino Audio (stub — real impl in audio_manager) ---

class ArduinoAudioStub final : public IStoryAudio {
 public:
  bool play(const char*, uint8_t, bool) override { return false; }
  void stop() override {}
  bool isPlaying() override { return false; }
  void setVolume(uint8_t) override {}
};

// --- Singleton instances ---

static ArduinoStorage s_storage;
static ArduinoLogger s_logger;
static ArduinoTimer s_timer;
static ArduinoAudioStub s_audio;

/// Call this in setup() to initialize the Arduino HAL.
void initArduinoHAL() {
  StoryHAL hal;
  hal.storage = &s_storage;
  hal.logger = &s_logger;
  hal.timer = &s_timer;
  hal.audio = &s_audio;
  setHAL(hal);
}

}  // namespace zacus_hal

#endif  // ARDUINO
