// app_audio.cpp - Audio Player Implementation
#include "app/app_audio.h"
#include "audio_manager.h"
#include "core/str_utils.h"

AppAudio g_app_audio;

bool AppAudio::init(const AppContext& ctx) {
  context_ = ctx;
  initialized_ = true;
  Serial.println("[APP_AUDIO] Initialized");
  return true;
}

void AppAudio::onStart() {
  current_state_ = STATE_IDLE;
  Serial.println("[APP_AUDIO] Started");
}

void AppAudio::onStop() {
  if (current_state_ == STATE_PLAYING || current_state_ == STATE_PAUSED) {
    stop();
  }
  current_state_ = STATE_IDLE;
  Serial.println("[APP_AUDIO] Stopped");
}

void AppAudio::onTick(uint32_t dt_ms) {
  if (!initialized_) return;
  
  // Update playback counter
  if (current_state_ == STATE_PLAYING) {
    playback_ms_ += dt_ms;
    
    // Check if we've reached end of track
    if (duration_ms_ > 0 && playback_ms_ >= duration_ms_) {
      stop();
    }
  }
  
  last_tick_ms_ = dt_ms;
}

void AppAudio::onAction(const AppAction& action) {
  if (!initialized_) return;
  
  Serial.printf("[APP_AUDIO] Action: %s payload=%s\n", action.name, action.payload);
  
  if (strcmp(action.name, "play") == 0) {
    play(action.payload);
  } else if (strcmp(action.name, "pause") == 0) {
    pause();
  } else if (strcmp(action.name, "resume") == 0) {
    resume();
  } else if (strcmp(action.name, "stop") == 0) {
    stop();
  } else if (strcmp(action.name, "volume") == 0) {
    int vol = static_cast<int>(strtol(action.payload, nullptr, 10));
    setVolume(constrain(vol, 0, 100));
  }
}

bool AppAudio::play(const char* filepath) {
  if (!filepath || strlen(filepath) == 0) {
    current_state_ = STATE_ERROR;
    return false;
  }
  
  core::copyText(current_file_, sizeof(current_file_), filepath);
  
  playback_ms_ = 0;
  duration_ms_ = 30000;  // Assume 30 seconds for demo
  current_state_ = STATE_PLAYING;
  
  Serial.printf("[APP_AUDIO] Playing: %s (vol=%u%%)\n", current_file_, volume_percent_);
  
  if (context_.audio) {
    context_.audio->play(filepath);
  }
  
  return true;
}

void AppAudio::pause() {
  if (current_state_ == STATE_PLAYING) {
    current_state_ = STATE_PAUSED;
    Serial.printf("[APP_AUDIO] Paused at %u ms\n", playback_ms_);
  }
}

void AppAudio::resume() {
  if (current_state_ == STATE_PAUSED) {
    current_state_ = STATE_PLAYING;
    Serial.println("[APP_AUDIO] Resumed");
  }
}

void AppAudio::stop() {
  current_state_ = STATE_IDLE;
  playback_ms_ = 0;
  duration_ms_ = 0;
  current_file_[0] = '\0';
  
  Serial.println("[APP_AUDIO] Stopped");
  
  if (context_.audio) {
    context_.audio->stop();
  }
}

void AppAudio::setVolume(uint8_t vol_percent) {
  volume_percent_ = vol_percent;
  Serial.printf("[APP_AUDIO] Volume: %u%%\n", volume_percent_);
  
  if (context_.audio) {
    context_.audio->setVolume(vol_percent);
  }
}

const char* AppAudio::getStatus() const {
  static char status_buf[96];
  const char* state_str = "IDLE";
  
  switch (current_state_) {
    case STATE_PLAYING:
      state_str = "PLAYING";
      break;
    case STATE_PAUSED:
      state_str = "PAUSED";
      break;
    case STATE_ERROR:
      state_str = "ERROR";
      break;
    default:
      state_str = "IDLE";
  }
  
  snprintf(status_buf, sizeof(status_buf),
           "state=%s file=%s timems=%u/%u vol=%u%%",
           state_str, current_file_, playback_ms_, duration_ms_, volume_percent_);
  
  return status_buf;
}

uint32_t AppAudio::getCurrentPlaybackMs() const {
  return playback_ms_;
}

uint32_t AppAudio::getTotalDurationMs() const {
  return duration_ms_;
}
