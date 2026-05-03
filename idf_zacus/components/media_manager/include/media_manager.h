// Zacus media_manager — ESP-IDF C port of the Arduino MediaManager class.
//
// Source of truth for the Arduino implementation:
//   ESP32_ZACUS/ui_freenove_allinone/src/system/media/media_manager.cpp
//
// The Arduino version is a C++ class with stateful catalog + I2S recorder
// helpers driven from the Freenove UI loop. The IDF port keeps the same
// runtime contract (catalog browsing, play/stop, fixed-duration WAV
// recording, snapshot read-out) but exposes it as a pure-C singleton
// because every consumer in the new firmware (NPC engine, voice pipeline,
// HTTP services) speaks C and we want to avoid a C++ runtime dependency
// on this layer.
//
// MP3 decoding and the I2S microphone capture path are deliberately stubbed
// in this slice — they pull in heavy managed components (esp-adf or
// audio_pipeline + helix-mp3, plus ES8388 codec bringup) that belong in
// their own dedicated slices. The stub still mounts LittleFS, opens the
// requested file to validate it exists, simulates a 2 s playback window
// (so callers can sequence cues end-to-end), and returns ESP_OK so the
// surrounding NPC coordination logic can be exercised today.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_PATH_MAX        128
#define MEDIA_DIR_MAX         32
#define MEDIA_ERROR_MAX       64
#define MEDIA_DEFAULT_RECORD_MAX_S  30U

// Configuration mirrors `MediaManager::Config` from the Arduino sources but
// with C strings instead of fixed char arrays embedded in a struct method.
typedef struct {
    char     music_dir[MEDIA_DIR_MAX];          // default "/littlefs/music"
    char     picture_dir[MEDIA_DIR_MAX];        // default "/littlefs/picture"
    char     record_dir[MEDIA_DIR_MAX];         // default "/littlefs/recorder"
    uint16_t record_max_seconds;                // default 30
    bool     auto_stop_record_on_step_change;   // default true
} media_manager_config_t;

// Snapshot mirrors `MediaManager::Snapshot`. Returned by value (cheap, ~400B).
typedef struct {
    bool     ready;
    bool     playing;
    bool     recording;
    bool     last_ok;
    bool     record_simulated;          // true while recorder remains stubbed
    uint16_t record_limit_seconds;
    uint16_t record_elapsed_seconds;
    uint32_t record_started_ms;
    char     playing_path[MEDIA_PATH_MAX];
    char     record_file[MEDIA_PATH_MAX];
    char     last_error[MEDIA_ERROR_MAX];
    char     music_dir[MEDIA_DIR_MAX];
    char     picture_dir[MEDIA_DIR_MAX];
    char     record_dir[MEDIA_DIR_MAX];
} media_manager_snapshot_t;

// Fill `cfg` with the defaults used by the Arduino firmware.
void media_manager_default_config(media_manager_config_t *cfg);

// Initialize the singleton media manager. Idempotent — re-initialization
// updates the configuration without losing the recorder state.
//
// Pre-conditions:
//   * LittleFS partition mounted at `cfg->music_dir` root (the manager will
//     create `music_dir`, `picture_dir`, `record_dir` if missing, but the
//     parent FS must exist first).
//
// Returns ESP_OK on success, ESP_ERR_INVALID_ARG on null config.
esp_err_t media_manager_init(const media_manager_config_t *cfg);

// Periodic tick — call from the main loop (Arduino did this from `loop()`).
// `now_ms` is a monotonic millisecond counter (use esp_timer_get_time/1000).
// Updates the simulated playback completion + recorder timeout.
void media_manager_update(uint32_t now_ms);

// Inform the manager that the active scenario step changed. When
// `auto_stop_record_on_step_change` is enabled this stops any recording
// in flight (matches Arduino behaviour).
void media_manager_note_step_change(void);

// Begin (simulated) playback of `path`. The path can be absolute (resolved
// as-is, e.g. "/littlefs/music/foo.mp3") or relative (resolved against
// `music_dir`). Returns ESP_OK if the file exists and ESP_ERR_NOT_FOUND
// otherwise; ESP_ERR_INVALID_ARG if `path` is null/empty.
//
// TODO(slice-4+): replace simulation with real I2S MP3 playback.
esp_err_t media_manager_play(const char *path);

// Stop any active (simulated) playback. Always succeeds.
esp_err_t media_manager_stop(void);

// Set the playback gain (0..100). Stored in the snapshot only — the stub
// playback path does not yet drive a codec. Returns ESP_OK or
// ESP_ERR_INVALID_ARG when value > 100.
esp_err_t media_manager_set_volume(uint8_t volume);

// Start recording up to `seconds` (clamped to `record_max_seconds`). The
// stubbed recorder allocates an empty WAV file at `record_dir/<filename>`
// so the file plumbing is exercised; future slices will plug the real I2S
// capture loop into `media_manager_update`.
//
// `filename_hint` may be null — a `record_<ms>.wav` name is generated.
esp_err_t media_manager_start_recording(uint16_t seconds,
                                        const char *filename_hint);

// Stop the active recording. Safe to call when not recording (returns OK).
esp_err_t media_manager_stop_recording(void);

// Copy the current snapshot into `out` (caller-owned). Useful for
// status endpoints.
void media_manager_snapshot(media_manager_snapshot_t *out);

#ifdef __cplusplus
}
#endif
