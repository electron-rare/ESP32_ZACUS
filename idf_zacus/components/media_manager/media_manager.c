// Zacus media_manager — IDF C port (slice 3, P1 voice pipeline migration).
//
// Mirrors the Arduino MediaManager class in
// ui_freenove_allinone/src/system/media/media_manager.cpp (~416 LOC C++).
//
// What is real here:
//   * Catalog directory bookkeeping (music / picture / record).
//   * `media_manager_play()` validates the file exists on LittleFS and
//     records the simulated-playback state into the snapshot.
//   * Recorder writes an empty WAV header so the file plumbing is real
//     and downstream consumers (NPC engine, voice bridge) can list /
//     fetch the recorder output.
//   * Step-change auto-stop hook matches Arduino behaviour.
//
// TODO(slice-4+): replace the stub with real I2S MP3 playback. The
// candidate paths are (a) ESP-ADF audio_pipeline + helix-mp3 decoder, or
// (b) a custom mini decoder reusing the helix-mp3 source already vendored
// in the Arduino tree. The decision belongs to the next slice that ports
// the AudioManager wrapper. Likewise, the I2S microphone capture path
// needs the ES8388 codec bringup before the recorder can deliver real PCM.

#include "media_manager.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "media_manager";

#define MEDIA_DEFAULT_MUSIC_DIR    "/littlefs/music"
#define MEDIA_DEFAULT_PICTURE_DIR  "/littlefs/picture"
#define MEDIA_DEFAULT_RECORD_DIR   "/littlefs/recorder"
#define MEDIA_RECORDER_SAMPLE_RATE 16000UL
#define MEDIA_RECORDER_BITS        16U
#define MEDIA_RECORDER_CHANNELS    1U

// Mirror Arduino's 2-second simulated playback window so callers can
// sequence cues without a real decoder behind us.
#define MEDIA_STUB_PLAYBACK_MS     2000U

// ─── Singleton state ─────────────────────────────────────────────────────────

static struct {
    bool                       initialized;
    media_manager_config_t     config;
    media_manager_snapshot_t   snapshot;
    uint8_t                    volume;          // 0..100
    uint32_t                   playback_ends_ms;
    FILE                      *recording_file;
    uint32_t                   recording_data_bytes;
} s_media;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void copy_text(char *dst, size_t dst_len, const char *src) {
    if (dst == NULL || dst_len == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

static void normalize_dir(char *out, size_t out_len, const char *src) {
    if (out == NULL || out_len == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        copy_text(out, out_len, "/");
        return;
    }
    // Skip leading whitespace.
    while (*src == ' ' || *src == '\t') {
        ++src;
    }
    if (src[0] == '\0') {
        copy_text(out, out_len, "/");
        return;
    }
    if (src[0] != '/') {
        snprintf(out, out_len, "/%s", src);
    } else {
        copy_text(out, out_len, src);
    }
    // Trim trailing slash unless root.
    size_t len = strlen(out);
    while (len > 1U && out[len - 1U] == '/') {
        out[len - 1U] = '\0';
        --len;
    }
}

static bool file_exists(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t ensure_dir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        return ESP_OK;
    }
    if (mkdir(path, 0777) == 0) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "mkdir(%s) failed: errno=%d", path, errno);
    return ESP_FAIL;
}

static void set_last_error(const char *msg) {
    s_media.snapshot.last_ok = false;
    copy_text(s_media.snapshot.last_error,
              sizeof(s_media.snapshot.last_error),
              msg != NULL ? msg : "media_unknown_error");
}

static void clear_last_error(void) {
    s_media.snapshot.last_ok = true;
    s_media.snapshot.last_error[0] = '\0';
}

// Resolve `path` against `music_dir` if relative; otherwise copy as-is.
static void resolve_play_path(char *out, size_t out_len, const char *path) {
    if (out == NULL || out_len == 0U) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (path[0] == '/') {
        copy_text(out, out_len, path);
    } else {
        snprintf(out, out_len, "%s/%s", s_media.config.music_dir, path);
    }
}

static void sanitize_filename(char *out, size_t out_len,
                              const char *hint, const char *default_prefix,
                              const char *extension) {
    if (out == NULL || out_len == 0U) {
        return;
    }
    if (hint == NULL || hint[0] == '\0') {
        const uint32_t now_ms =
            (uint32_t) (esp_timer_get_time() / 1000LL);
        snprintf(out, out_len, "%s_%lu", default_prefix,
                 (unsigned long) now_ms);
    } else {
        copy_text(out, out_len, hint);
    }
    // Replace anything not [A-Za-z0-9_.-] with '_'.
    for (size_t i = 0U; out[i] != '\0'; ++i) {
        const unsigned char ch = (unsigned char) out[i];
        const bool keep = isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
        if (!keep) {
            out[i] = '_';
        }
    }
    if (extension != NULL && extension[0] != '\0') {
        const size_t cur_len = strlen(out);
        const size_t ext_len = strlen(extension);
        if (cur_len < ext_len ||
            strcmp(out + cur_len - ext_len, extension) != 0) {
            if (cur_len + ext_len < out_len) {
                strcat(out, extension);
            }
        }
    }
}

// Write a minimal RIFF/WAVE header with `data_size` bytes (0 means "open"
// header, will be patched by stop_recording).
static bool write_wav_header(FILE *f, uint32_t data_size) {
    if (f == NULL) {
        return false;
    }
    const uint32_t byte_rate =
        MEDIA_RECORDER_SAMPLE_RATE * MEDIA_RECORDER_CHANNELS *
        (MEDIA_RECORDER_BITS / 8U);
    const uint16_t block_align =
        (uint16_t) (MEDIA_RECORDER_CHANNELS * (MEDIA_RECORDER_BITS / 8U));
    const uint32_t chunk_size = 36U + data_size;
    const uint32_t fmt_size = 16U;
    const uint16_t audio_format = 1U;            // PCM
    const uint16_t channels = MEDIA_RECORDER_CHANNELS;
    const uint32_t sample_rate = MEDIA_RECORDER_SAMPLE_RATE;
    const uint16_t bits = MEDIA_RECORDER_BITS;

    if (fseek(f, 0, SEEK_SET) != 0) {
        return false;
    }
    if (fwrite("RIFF", 1, 4, f) != 4) return false;
    if (fwrite(&chunk_size, sizeof(chunk_size), 1, f) != 1) return false;
    if (fwrite("WAVE", 1, 4, f) != 4) return false;
    if (fwrite("fmt ", 1, 4, f) != 4) return false;
    if (fwrite(&fmt_size, sizeof(fmt_size), 1, f) != 1) return false;
    if (fwrite(&audio_format, sizeof(audio_format), 1, f) != 1) return false;
    if (fwrite(&channels, sizeof(channels), 1, f) != 1) return false;
    if (fwrite(&sample_rate, sizeof(sample_rate), 1, f) != 1) return false;
    if (fwrite(&byte_rate, sizeof(byte_rate), 1, f) != 1) return false;
    if (fwrite(&block_align, sizeof(block_align), 1, f) != 1) return false;
    if (fwrite(&bits, sizeof(bits), 1, f) != 1) return false;
    if (fwrite("data", 1, 4, f) != 4) return false;
    if (fwrite(&data_size, sizeof(data_size), 1, f) != 1) return false;
    return true;
}

// ─── Public API ──────────────────────────────────────────────────────────────

void media_manager_default_config(media_manager_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    copy_text(cfg->music_dir,   sizeof(cfg->music_dir),   MEDIA_DEFAULT_MUSIC_DIR);
    copy_text(cfg->picture_dir, sizeof(cfg->picture_dir), MEDIA_DEFAULT_PICTURE_DIR);
    copy_text(cfg->record_dir,  sizeof(cfg->record_dir),  MEDIA_DEFAULT_RECORD_DIR);
    cfg->record_max_seconds = MEDIA_DEFAULT_RECORD_MAX_S;
    cfg->auto_stop_record_on_step_change = true;
}

esp_err_t media_manager_init(const media_manager_config_t *cfg) {
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Apply config + normalize dirs.
    media_manager_config_t normalized = *cfg;
    char tmp[MEDIA_DIR_MAX];
    normalize_dir(tmp, sizeof(tmp), cfg->music_dir);
    copy_text(normalized.music_dir, sizeof(normalized.music_dir), tmp);
    normalize_dir(tmp, sizeof(tmp), cfg->picture_dir);
    copy_text(normalized.picture_dir, sizeof(normalized.picture_dir), tmp);
    normalize_dir(tmp, sizeof(tmp), cfg->record_dir);
    copy_text(normalized.record_dir, sizeof(normalized.record_dir), tmp);
    if (normalized.record_max_seconds == 0U) {
        normalized.record_max_seconds = MEDIA_DEFAULT_RECORD_MAX_S;
    }
    if (normalized.record_max_seconds > 1800U) {
        normalized.record_max_seconds = 1800U;
    }

    s_media.config = normalized;
    if (!s_media.initialized) {
        s_media.volume = 80U;
        s_media.playback_ends_ms = 0U;
        s_media.recording_file = NULL;
        s_media.recording_data_bytes = 0U;
    }

    memset(&s_media.snapshot, 0, sizeof(s_media.snapshot));
    s_media.snapshot.ready = true;
    s_media.snapshot.last_ok = true;
    s_media.snapshot.record_simulated = true;   // recorder is stubbed
    s_media.snapshot.record_limit_seconds = normalized.record_max_seconds;
    copy_text(s_media.snapshot.music_dir, sizeof(s_media.snapshot.music_dir),
              normalized.music_dir);
    copy_text(s_media.snapshot.picture_dir, sizeof(s_media.snapshot.picture_dir),
              normalized.picture_dir);
    copy_text(s_media.snapshot.record_dir, sizeof(s_media.snapshot.record_dir),
              normalized.record_dir);

    (void) ensure_dir(normalized.music_dir);
    (void) ensure_dir(normalized.picture_dir);
    (void) ensure_dir(normalized.record_dir);

    s_media.initialized = true;
    ESP_LOGI(TAG, "init music=%s picture=%s record=%s rec_max=%us",
             normalized.music_dir, normalized.picture_dir,
             normalized.record_dir, normalized.record_max_seconds);
    ESP_LOGW(TAG,
             "playback + capture are STUBBED — see TODO in media_manager.c");
    return ESP_OK;
}

void media_manager_update(uint32_t now_ms) {
    if (!s_media.initialized) {
        return;
    }
    // Simulated playback: clear `playing` after the stub window elapses.
    if (s_media.snapshot.playing &&
        s_media.playback_ends_ms != 0U &&
        (int32_t) (now_ms - s_media.playback_ends_ms) >= 0) {
        ESP_LOGI(TAG, "simulated playback finished: %s",
                 s_media.snapshot.playing_path);
        s_media.snapshot.playing = false;
        s_media.snapshot.playing_path[0] = '\0';
        s_media.playback_ends_ms = 0U;
    }
    // Recorder timeout (stub: data bytes never grow, but the timer mirrors
    // Arduino behaviour so callers can rely on auto-stop).
    if (s_media.snapshot.recording) {
        const uint32_t elapsed_ms = now_ms - s_media.snapshot.record_started_ms;
        s_media.snapshot.record_elapsed_seconds =
            (uint16_t) (elapsed_ms / 1000U);
        if (s_media.snapshot.record_limit_seconds > 0U &&
            s_media.snapshot.record_elapsed_seconds >=
                s_media.snapshot.record_limit_seconds) {
            (void) media_manager_stop_recording();
        }
    }
}

void media_manager_note_step_change(void) {
    if (!s_media.initialized) {
        return;
    }
    if (s_media.config.auto_stop_record_on_step_change &&
        s_media.snapshot.recording) {
        (void) media_manager_stop_recording();
    }
}

esp_err_t media_manager_play(const char *path) {
    if (!s_media.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL || path[0] == '\0') {
        set_last_error("media_play_invalid_args");
        return ESP_ERR_INVALID_ARG;
    }

    char resolved[MEDIA_PATH_MAX];
    resolve_play_path(resolved, sizeof(resolved), path);
    if (resolved[0] == '\0') {
        set_last_error("media_play_empty_path");
        return ESP_ERR_INVALID_ARG;
    }

    if (!file_exists(resolved)) {
        ESP_LOGW(TAG, "play(%s): file not found", resolved);
        set_last_error("media_play_not_found");
        return ESP_ERR_NOT_FOUND;
    }

    // TODO(slice-4+): hand off to the real I2S MP3 decoder here.
    const uint32_t now_ms = (uint32_t) (esp_timer_get_time() / 1000LL);
    s_media.snapshot.playing = true;
    copy_text(s_media.snapshot.playing_path,
              sizeof(s_media.snapshot.playing_path), resolved);
    s_media.playback_ends_ms = now_ms + MEDIA_STUB_PLAYBACK_MS;
    clear_last_error();
    ESP_LOGI(TAG, "playing %s (simulated %ums) vol=%u",
             resolved, MEDIA_STUB_PLAYBACK_MS, s_media.volume);
    return ESP_OK;
}

esp_err_t media_manager_stop(void) {
    if (!s_media.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_media.snapshot.playing) {
        ESP_LOGI(TAG, "stop %s", s_media.snapshot.playing_path);
    }
    s_media.snapshot.playing = false;
    s_media.snapshot.playing_path[0] = '\0';
    s_media.playback_ends_ms = 0U;
    clear_last_error();
    return ESP_OK;
}

esp_err_t media_manager_set_volume(uint8_t volume) {
    if (volume > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    s_media.volume = volume;
    ESP_LOGI(TAG, "volume=%u", volume);
    return ESP_OK;
}

esp_err_t media_manager_start_recording(uint16_t seconds,
                                        const char *filename_hint) {
    if (!s_media.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_media.snapshot.recording) {
        set_last_error("recorder_already_running");
        return ESP_ERR_INVALID_STATE;
    }
    if (seconds == 0U) {
        seconds = s_media.config.record_max_seconds;
    }
    if (seconds > s_media.config.record_max_seconds) {
        seconds = s_media.config.record_max_seconds;
    }
    if (seconds == 0U) {
        seconds = 1U;
    }
    if (ensure_dir(s_media.config.record_dir) != ESP_OK) {
        set_last_error("recorder_dir_missing");
        return ESP_FAIL;
    }

    // Filename bounded so the concatenation below cannot overflow `path`
    // (record_dir <= MEDIA_DIR_MAX, plus '/' separator, plus filename).
    char filename[MEDIA_PATH_MAX - MEDIA_DIR_MAX - 1];
    sanitize_filename(filename, sizeof(filename),
                      filename_hint, "record", ".wav");
    char path[MEDIA_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s",
             s_media.config.record_dir, filename);

    if (s_media.recording_file != NULL) {
        fclose(s_media.recording_file);
        s_media.recording_file = NULL;
    }
    s_media.recording_file = fopen(path, "wb+");
    if (s_media.recording_file == NULL) {
        ESP_LOGW(TAG, "fopen(%s) failed: errno=%d", path, errno);
        set_last_error("recorder_create_failed");
        return ESP_FAIL;
    }
    if (!write_wav_header(s_media.recording_file, 0U)) {
        fclose(s_media.recording_file);
        s_media.recording_file = NULL;
        set_last_error("recorder_header_failed");
        return ESP_FAIL;
    }
    s_media.recording_data_bytes = 0U;

    const uint32_t now_ms = (uint32_t) (esp_timer_get_time() / 1000LL);
    s_media.snapshot.recording = true;
    s_media.snapshot.record_limit_seconds = seconds;
    s_media.snapshot.record_started_ms = now_ms;
    s_media.snapshot.record_elapsed_seconds = 0U;
    copy_text(s_media.snapshot.record_file,
              sizeof(s_media.snapshot.record_file), path);
    clear_last_error();
    ESP_LOGI(TAG, "recording started -> %s (limit=%us, simulated PCM)",
             path, seconds);
    return ESP_OK;
}

esp_err_t media_manager_stop_recording(void) {
    if (!s_media.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_media.snapshot.recording) {
        return ESP_OK;
    }
    if (s_media.recording_file != NULL) {
        // Patch header with the real (zero, in stub mode) data length.
        (void) write_wav_header(s_media.recording_file,
                                s_media.recording_data_bytes);
        fclose(s_media.recording_file);
        s_media.recording_file = NULL;
    }
    const uint32_t now_ms = (uint32_t) (esp_timer_get_time() / 1000LL);
    const uint32_t elapsed_ms = now_ms - s_media.snapshot.record_started_ms;
    s_media.snapshot.record_elapsed_seconds = (uint16_t) (elapsed_ms / 1000U);
    s_media.snapshot.recording = false;
    clear_last_error();
    ESP_LOGI(TAG, "recording stopped (%us elapsed, file=%s)",
             s_media.snapshot.record_elapsed_seconds,
             s_media.snapshot.record_file);
    return ESP_OK;
}

void media_manager_snapshot(media_manager_snapshot_t *out) {
    if (out == NULL) {
        return;
    }
    *out = s_media.snapshot;
}
