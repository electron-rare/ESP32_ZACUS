// audio_kit_client.h - HTTP client for Audio Kit ESP32 telephone module.
// BOX-3 sends commands to Audio Kit over LAN HTTP (port 8300).
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio Kit telephone status
 */
typedef struct {
    bool playing;
    bool phone_off_hook;
    bool button_pressed;
    int wifi_rssi;
} audio_kit_status_t;

/**
 * @brief Initialize Audio Kit HTTP client
 * @param base_url e.g. "http://192.168.0.XXX:8300"
 */
esp_err_t audio_kit_client_init(const char *base_url);

/**
 * @brief Play TTS on Audio Kit telephone speaker
 * @param text French text to synthesize
 * @param tts_url Piper TTS endpoint (e.g. "http://192.168.0.120:8001")
 * @param voice Voice name (e.g. "tom-medium")
 */
esp_err_t audio_kit_play_tts(const char *text, const char *tts_url, const char *voice);

/**
 * @brief Play MP3 from Audio Kit SD card
 * @param sd_path Path on SD card (e.g. "/hotline_tts/hints__SCENE_LA__level_1__0.mp3")
 */
esp_err_t audio_kit_play_sd(const char *sd_path);

/**
 * @brief Stop current playback
 */
esp_err_t audio_kit_stop(void);

/**
 * @brief Ring the telephone
 * @param pattern Ring pattern ("intro", "hint", "urgent")
 */
esp_err_t audio_kit_ring(const char *pattern);

/**
 * @brief Poll Audio Kit status (call every 500ms)
 */
esp_err_t audio_kit_poll_status(audio_kit_status_t *status);

/**
 * @brief Check if Audio Kit is reachable
 */
bool audio_kit_is_healthy(void);

#ifdef __cplusplus
}
#endif
