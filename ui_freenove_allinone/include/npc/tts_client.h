// tts_client.h - HTTP client for Piper TTS on Tower:8001 with SD fallback.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTS_TOWER_IP        "192.168.0.120"
#define TTS_TOWER_PORT      8001
#define TTS_API_PATH        "/api/tts"
#define TTS_VOICE           "tom-medium"
#define TTS_TIMEOUT_MS      2000
#define TTS_HEALTH_INTERVAL_MS  30000
#define TTS_MAX_TEXT_LEN    200
#define TTS_WAV_BUF_SIZE    (64 * 1024)

typedef enum {
    TTS_RESULT_OK = 0,
    TTS_RESULT_TIMEOUT,
    TTS_RESULT_HTTP_ERROR,
    TTS_RESULT_ALLOC_FAIL,
    TTS_RESULT_TOWER_DOWN,
    TTS_RESULT_TEXT_TOO_LONG
} tts_result_t;

typedef struct {
    bool tower_reachable;
    uint32_t last_health_check_ms;
    uint32_t last_latency_ms;
    uint32_t total_requests;
    uint32_t total_failures;
} tts_stats_t;

/// Initialize TTS client (call once from setup).
void tts_init(void);

/// Check Tower TTS health (non-blocking, HEAD request).
/// Updates internal reachability state.
/// Returns true if Tower responded within TTS_TIMEOUT_MS.
bool tts_check_health(uint32_t now_ms);

/// Periodic health check — only pings if TTS_HEALTH_INTERVAL_MS elapsed.
void tts_health_tick(uint32_t now_ms);

/// Query last known Tower reachability.
bool tts_is_tower_reachable(void);

/// Request TTS synthesis. Blocking call (up to TTS_TIMEOUT_MS).
/// On success, writes WAV data to out_buf and sets *out_len.
/// Caller must allocate out_buf of at least TTS_WAV_BUF_SIZE bytes (use PSRAM).
tts_result_t tts_synthesize(const char* text, uint8_t* out_buf,
                            size_t buf_capacity, size_t* out_len);

/// Get TTS client statistics.
tts_stats_t tts_get_stats(void);

#ifdef __cplusplus
}
#endif
