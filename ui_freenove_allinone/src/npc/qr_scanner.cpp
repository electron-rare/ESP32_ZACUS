// qr_scanner.cpp - ZACUS QR protocol parser, validator, anti-cheat.
#include "npc/qr_scanner.h"
#include <cstring>
#include <cstdio>
#include <mbedtls/md.h>

typedef struct {
    char scene_id[QR_SCENE_ID_MAX];
    char event_id[QR_EVENT_ID_MAX];
    uint32_t at_ms;
} qr_history_entry_t;

static qr_history_entry_t s_history[QR_MAX_HISTORY];
static uint8_t s_history_count = 0;
static uint32_t s_last_scan_ms = 0;
static char s_last_payload[QR_SCENE_ID_MAX + QR_EVENT_ID_MAX + 8] = {0};

void qr_npc_init(void) {
    memset(s_history, 0, sizeof(s_history));
    s_history_count = 0;
    s_last_scan_ms = 0;
    s_last_payload[0] = '\0';
}

bool qr_npc_parse(const char* payload, qr_decode_result_t* out) {
    if (payload == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));

    // Expected: ZACUS:{scene_id}:{event_id}:{checksum}
    if (strncmp(payload, QR_PREFIX, QR_PREFIX_LEN) != 0) {
        out->status = QR_INVALID_FORMAT;
        return false;
    }

    const char* p = payload + QR_PREFIX_LEN;

    // Parse scene_id
    const char* sep1 = strchr(p, ':');
    if (sep1 == NULL) { out->status = QR_INVALID_FORMAT; return false; }
    size_t scene_len = (size_t)(sep1 - p);
    if (scene_len == 0 || scene_len >= QR_SCENE_ID_MAX) {
        out->status = QR_INVALID_FORMAT; return false;
    }
    memcpy(out->scene_id, p, scene_len);
    out->scene_id[scene_len] = '\0';

    // Parse event_id
    const char* p2 = sep1 + 1;
    const char* sep2 = strchr(p2, ':');
    if (sep2 == NULL) { out->status = QR_INVALID_FORMAT; return false; }
    size_t event_len = (size_t)(sep2 - p2);
    if (event_len == 0 || event_len >= QR_EVENT_ID_MAX) {
        out->status = QR_INVALID_FORMAT; return false;
    }
    memcpy(out->event_id, p2, event_len);
    out->event_id[event_len] = '\0';

    // Parse checksum (4 hex chars)
    const char* p3 = sep2 + 1;
    size_t ck_len = strlen(p3);
    if (ck_len != QR_CHECKSUM_LEN) {
        out->status = QR_INVALID_FORMAT; return false;
    }
    memcpy(out->checksum, p3, QR_CHECKSUM_LEN);
    out->checksum[QR_CHECKSUM_LEN] = '\0';

    out->status = QR_VALID;
    return true;
}

void qr_npc_compute_checksum(const char* scene_id, const char* event_id,
                              char out_checksum[5]) {
    if (scene_id == NULL || event_id == NULL || out_checksum == NULL) return;

    char input[QR_SCENE_ID_MAX + QR_EVENT_ID_MAX + 2];
    snprintf(input, sizeof(input), "%s:%s", scene_id, event_id);

    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)QR_HMAC_KEY, strlen(QR_HMAC_KEY));
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)input, strlen(input));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    // Truncate to first 2 bytes = 4 hex chars
    snprintf(out_checksum, 5, "%02X%02X", hmac[0], hmac[1]);
}

qr_validation_t qr_npc_validate(const qr_decode_result_t* decoded,
                                 const char* current_scene_id,
                                 uint32_t now_ms) {
    if (decoded == NULL || decoded->status != QR_VALID) {
        return QR_INVALID_FORMAT;
    }

    // Checksum verification
    char expected[5];
    qr_npc_compute_checksum(decoded->scene_id, decoded->event_id, expected);
    if (strncmp(expected, decoded->checksum, QR_CHECKSUM_LEN) != 0) {
        return QR_INVALID_CHECKSUM;
    }

    // Scene check: QR only valid for its designated scene
    if (current_scene_id != NULL
        && strcmp(decoded->scene_id, current_scene_id) != 0) {
        return QR_WRONG_SCENE;
    }

    // Already scanned check
    if (qr_npc_was_scanned(decoded->scene_id, decoded->event_id)) {
        return QR_ALREADY_SCANNED;
    }

    // Debounce: same raw payload within QR_DEBOUNCE_MS
    char key[QR_SCENE_ID_MAX + QR_EVENT_ID_MAX + 2];
    snprintf(key, sizeof(key), "%s:%s", decoded->scene_id, decoded->event_id);
    if (strcmp(key, s_last_payload) == 0
        && (now_ms - s_last_scan_ms) < QR_DEBOUNCE_MS) {
        return QR_DEBOUNCED;
    }

    return QR_VALID;
}

void qr_npc_record_scan(const char* scene_id, const char* event_id, uint32_t now_ms) {
    if (scene_id == NULL || event_id == NULL) return;

    // Update debounce state
    snprintf(s_last_payload, sizeof(s_last_payload), "%s:%s", scene_id, event_id);
    s_last_scan_ms = now_ms;

    // Add to history (ring buffer)
    if (s_history_count < QR_MAX_HISTORY) {
        qr_history_entry_t* entry = &s_history[s_history_count++];
        strncpy(entry->scene_id, scene_id, QR_SCENE_ID_MAX - 1);
        strncpy(entry->event_id, event_id, QR_EVENT_ID_MAX - 1);
        entry->at_ms = now_ms;
    }
}

bool qr_npc_was_scanned(const char* scene_id, const char* event_id) {
    if (scene_id == NULL || event_id == NULL) return false;
    for (uint8_t i = 0; i < s_history_count; i++) {
        if (strcmp(s_history[i].scene_id, scene_id) == 0
            && strcmp(s_history[i].event_id, event_id) == 0) {
            return true;
        }
    }
    return false;
}

uint8_t qr_npc_scan_count(void) {
    return s_history_count;
}
