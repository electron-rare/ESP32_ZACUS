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
