// tts_client.cpp - Piper TTS HTTP client for ESP32.
#include "npc/tts_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>

static tts_stats_t s_stats = {};
static char s_url[128] = {0};

void tts_init(void) {
    memset(&s_stats, 0, sizeof(s_stats));
    snprintf(s_url, sizeof(s_url), "http://%s:%d%s",
             TTS_TOWER_IP, TTS_TOWER_PORT, TTS_API_PATH);
    Serial.printf("[TTS] init url=%s\n", s_url);
}

bool tts_check_health(uint32_t now_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        s_stats.tower_reachable = false;
        s_stats.last_health_check_ms = now_ms;
        return false;
    }

    HTTPClient http;
    http.setTimeout(TTS_TIMEOUT_MS);

    char health_url[128];
    snprintf(health_url, sizeof(health_url), "http://%s:%d/",
             TTS_TOWER_IP, TTS_TOWER_PORT);

    bool ok = false;
    if (http.begin(health_url)) {
        uint32_t t0 = millis();
        int code = http.sendRequest("HEAD");
        s_stats.last_latency_ms = millis() - t0;
        ok = (code >= 200 && code < 400);
        http.end();
    }

    s_stats.tower_reachable = ok;
    s_stats.last_health_check_ms = now_ms;
    Serial.printf("[TTS] health: %s (%lu ms)\n",
                  ok ? "OK" : "DOWN", (unsigned long)s_stats.last_latency_ms);
    return ok;
}

void tts_health_tick(uint32_t now_ms) {
    if (now_ms - s_stats.last_health_check_ms >= TTS_HEALTH_INTERVAL_MS) {
        tts_check_health(now_ms);
    }
}

bool tts_is_tower_reachable(void) {
    return s_stats.tower_reachable;
}

tts_result_t tts_synthesize(const char* text, uint8_t* out_buf,
                            size_t buf_capacity, size_t* out_len) {
    if (text == NULL || out_buf == NULL || out_len == NULL) {
        return TTS_RESULT_ALLOC_FAIL;
    }
    *out_len = 0;

    size_t text_len = strlen(text);
    if (text_len > TTS_MAX_TEXT_LEN) {
        return TTS_RESULT_TEXT_TOO_LONG;
    }
    if (!s_stats.tower_reachable) {
        return TTS_RESULT_TOWER_DOWN;
    }

    s_stats.total_requests++;

    HTTPClient http;
    http.setTimeout(TTS_TIMEOUT_MS);

    if (!http.begin(s_url)) {
        s_stats.total_failures++;
        return TTS_RESULT_HTTP_ERROR;
    }

    http.addHeader("Content-Type", "application/json");

    // Build JSON body: {"text":"...","voice":"tom-medium","format":"wav"}
    char body[TTS_MAX_TEXT_LEN + 64];
    snprintf(body, sizeof(body),
             "{\"text\":\"%s\",\"voice\":\"%s\",\"format\":\"wav\"}",
             text, TTS_VOICE);

    uint32_t t0 = millis();
    int code = http.POST(body);
    s_stats.last_latency_ms = millis() - t0;

    if (code != 200) {
        Serial.printf("[TTS] POST failed: %d (%lu ms)\n", code,
                      (unsigned long)s_stats.last_latency_ms);
        http.end();
        s_stats.total_failures++;
        if (s_stats.last_latency_ms >= TTS_TIMEOUT_MS) {
            return TTS_RESULT_TIMEOUT;
        }
        return TTS_RESULT_HTTP_ERROR;
    }

    int content_len = http.getSize();
    if (content_len <= 0 || (size_t)content_len > buf_capacity) {
        Serial.printf("[TTS] bad content_len: %d (cap %u)\n",
                      content_len, (unsigned)buf_capacity);
        http.end();
        s_stats.total_failures++;
        return TTS_RESULT_ALLOC_FAIL;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total_read = 0;
    while (total_read < (size_t)content_len) {
        int avail = stream->available();
        if (avail <= 0) {
            if (!stream->connected()) break;
            delay(1);
            continue;
        }
        int to_read = (avail > 1024) ? 1024 : avail;
        if (total_read + (size_t)to_read > buf_capacity) {
            to_read = (int)(buf_capacity - total_read);
        }
        int r = stream->read(out_buf + total_read, to_read);
        if (r > 0) total_read += (size_t)r;
    }

    http.end();
    *out_len = total_read;
    Serial.printf("[TTS] OK: %u bytes, %lu ms\n",
                  (unsigned)total_read, (unsigned long)s_stats.last_latency_ms);
    return TTS_RESULT_OK;
}

tts_stats_t tts_get_stats(void) {
    return s_stats;
}

// ============================================================
// V3 fallback chain: XTTS-v2 → Piper → SD card
// ============================================================

static tts_backend_t s_last_backend = TTS_BACKEND_XTTS;
static bool s_xtts_reachable  = false;
static uint32_t s_xtts_last_check_ms = 0;

// Check a generic HTTP endpoint for reachability (HEAD /health).
static bool tts_check_url(const char* url) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setTimeout(TTS_TIMEOUT_MS);
    bool ok = false;
    if (http.begin(url)) {
        int code = http.sendRequest("GET");
        ok = (code >= 200 && code < 400);
        http.end();
    }
    return ok;
}

// Synthesize via HTTP endpoint (shared logic for XTTS and Piper).
// url: full endpoint URL (e.g. "http://kxkm-ai:5002/v1/audio/speech")
// text: NPC phrase text
static tts_result_t tts_speak_http(const char* url, const char* text,
                                    uint8_t* out_buf, size_t buf_capacity,
                                    size_t* out_len) {
    if (!url || !text || !out_buf || !out_len) return TTS_RESULT_ALLOC_FAIL;
    *out_len = 0;

    if (strlen(text) > TTS_MAX_TEXT_LEN) return TTS_RESULT_TEXT_TOO_LONG;

    s_stats.total_requests++;

    HTTPClient http;
    http.setTimeout(TTS_TIMEOUT_MS);
    if (!http.begin(url)) {
        s_stats.total_failures++;
        return TTS_RESULT_HTTP_ERROR;
    }

    http.addHeader("Content-Type", "application/json");

    // OpenAI-compatible JSON body
    char body[TTS_MAX_TEXT_LEN + 64];
    snprintf(body, sizeof(body), "{\"input\":\"%s\",\"voice\":\"%s\"}", text, TTS_VOICE);

    uint32_t t0 = millis();
    int code = http.POST(body);
    s_stats.last_latency_ms = millis() - t0;

    if (code != 200) {
        Serial.printf("[TTS_V3] POST failed: %d (%lu ms)\n", code,
                      (unsigned long)s_stats.last_latency_ms);
        http.end();
        s_stats.total_failures++;
        return (s_stats.last_latency_ms >= (uint32_t)TTS_TIMEOUT_MS)
               ? TTS_RESULT_TIMEOUT : TTS_RESULT_HTTP_ERROR;
    }

    int content_len = http.getSize();
    if (content_len <= 0 || (size_t)content_len > buf_capacity) {
        Serial.printf("[TTS_V3] bad content_len: %d\n", content_len);
        http.end();
        s_stats.total_failures++;
        return TTS_RESULT_ALLOC_FAIL;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total_read = 0;
    while (total_read < (size_t)content_len) {
        int avail = stream->available();
        if (avail <= 0) {
            if (!stream->connected()) break;
            delay(1);
            continue;
        }
        int to_read = (avail > 1024) ? 1024 : avail;
        if (total_read + (size_t)to_read > buf_capacity)
            to_read = (int)(buf_capacity - total_read);
        int r = stream->read(out_buf + total_read, to_read);
        if (r > 0) total_read += (size_t)r;
    }

    http.end();
    *out_len = total_read;
    Serial.printf("[TTS_V3] OK via %s: %u bytes, %lu ms\n", url,
                  (unsigned)total_read, (unsigned long)s_stats.last_latency_ms);
    return TTS_RESULT_OK;
}

tts_backend_t tts_select_backend(uint32_t now_ms) {
    // Refresh XTTS reachability if interval elapsed
    if (now_ms - s_xtts_last_check_ms >= TTS_HEALTH_INTERVAL_MS) {
        char xtts_health[64];
        snprintf(xtts_health, sizeof(xtts_health),
                 "http://%s:%d/health", TTS_XTTS_HOST, TTS_XTTS_PORT);
        s_xtts_reachable = tts_check_url(xtts_health);
        s_xtts_last_check_ms = now_ms;
        Serial.printf("[TTS_V3] XTTS health: %s\n",
                      s_xtts_reachable ? "OK" : "DOWN");
    }

    if (s_xtts_reachable) return TTS_BACKEND_XTTS;
    if (s_stats.tower_reachable) return TTS_BACKEND_PIPER;
    return TTS_BACKEND_SD;
}

tts_result_t tts_speak_v3(const char* text, const char* sd_fallback_key,
                           uint8_t* out_buf, size_t buf_capacity, size_t* out_len) {
    uint32_t now = millis();
    tts_backend_t backend = tts_select_backend(now);
    s_last_backend = backend;

    if (backend == TTS_BACKEND_XTTS) {
        char url[64];
        snprintf(url, sizeof(url), "http://%s:%d%s",
                 TTS_XTTS_HOST, TTS_XTTS_PORT, TTS_XTTS_API_PATH);
        tts_result_t r = tts_speak_http(url, text, out_buf, buf_capacity, out_len);
        if (r == TTS_RESULT_OK) return TTS_RESULT_OK;
        // XTTS failed: fall through to Piper
        Serial.printf("[TTS_V3] XTTS failed (%d), falling back to Piper\n", r);
        s_xtts_reachable = false;
        backend = TTS_BACKEND_PIPER;
        s_last_backend = backend;
    }

    if (backend == TTS_BACKEND_PIPER) {
        char url[64];
        snprintf(url, sizeof(url), "http://%s:%d%s",
                 TTS_TOWER_IP, TTS_TOWER_PORT, TTS_PIPER_API_PATH);
        tts_result_t r = tts_speak_http(url, text, out_buf, buf_capacity, out_len);
        if (r == TTS_RESULT_OK) return TTS_RESULT_OK;
        // Piper failed: fall through to SD
        Serial.printf("[TTS_V3] Piper failed (%d), falling back to SD\n", r);
        s_stats.tower_reachable = false;
        backend = TTS_BACKEND_SD;
        s_last_backend = backend;
    }

    // SD card fallback: play pre-generated file from hotline_tts/
    if (sd_fallback_key && sd_fallback_key[0]) {
        char sd_path[128];
        // Convert key dots to slashes: hints.P1_SON.level_1.0 → /hotline_tts/hints/P1_SON/level_1/0.wav
        snprintf(sd_path, sizeof(sd_path), "/hotline_tts/%s.wav", sd_fallback_key);
        // Replace dots with slashes inside the key portion
        char* key_start = sd_path + strlen("/hotline_tts/");
        for (char* p = key_start; *p; p++) {
            if (*p == '.') *p = '/';
        }
        Serial.printf("[TTS_V3] SD fallback: %s\n", sd_path);
        // Caller must implement tts_play_sd_file(sd_path) — return OK as signal
        // to use sd_path. out_len = 0 indicates SD path should be used.
        if (out_len) *out_len = 0;
        return TTS_RESULT_TOWER_DOWN;  // caller checks last_backend == SD
    }

    return TTS_RESULT_TOWER_DOWN;
}

tts_backend_t tts_last_backend(void) {
    return s_last_backend;
}
