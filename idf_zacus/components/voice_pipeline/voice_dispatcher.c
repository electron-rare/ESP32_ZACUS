// voice_dispatcher — see voice_dispatcher.h for the slice-8 contract.

#include "voice_dispatcher.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "npc_engine.h"

static const char *TAG = "voice_disp";

// Slice 8 keyword fast-path. Matched as case-insensitive ASCII-folded
// substrings of the normalized STT text. Adding more variants here is
// cheap (linear scan, ~8 entries). Keep this list FR-only for now —
// we'll branch on a locale flag once the bridge sends one.
static const char *const HINT_KEYWORDS[] = {
    "indice",
    "aide",
    "hint",
    "bloque",        // matches both "bloqué" and "bloque" after fold
    "perdu",         // matches "perdu" / "perdue" via substring
    "comment faire",
    "je sais pas",
    "sais pas",
};
static const size_t HINT_KEYWORDS_COUNT =
    sizeof(HINT_KEYWORDS) / sizeof(HINT_KEYWORDS[0]);

// Placeholder puzzle id used while the scenario engine doesn't yet
// surface the active puzzle context. The hints engine treats id == 0
// as "decide based on current scene context" so this stays usable
// end-to-end. See P1 voice-pipeline spec §slice-8.
#define DISPATCHER_PLACEHOLDER_PUZZLE_ID  0
// level == 0 = let the hints engine pick the escalation level via its
// adaptive policy. See specs/AI_INTEGRATION_SPEC.md.
#define DISPATCHER_DEFAULT_HINT_LEVEL     0

static bool s_initialized = false;

// ── ASCII-fold helpers ──────────────────────────────────────────────
//
// We receive UTF-8 from the bridge. The set of characters we care about
// for FR keyword matching is small; instead of pulling in a full
// Unicode lib we hand-fold the common diacritics we expect to see in
// transcribed French. Anything we don't recognise either passes through
// untouched (ASCII) or gets dropped (multi-byte unknowns).
//
// Mapping table (hex = first/second UTF-8 byte):
//   é è ê ë → e        (C3 A9 / C3 A8 / C3 AA / C3 AB)
//   à â ä   → a        (C3 A0 / C3 A2 / C3 A4)
//   î ï     → i        (C3 AE / C3 AF)
//   ô ö     → o        (C3 B4 / C3 B6)
//   ù û ü   → u        (C3 B9 / C3 BB / C3 BC)
//   ç       → c        (C3 A7)
//   œ       → oe       (C5 93)
//   É È Ê Ë → e (uppercase versions, second byte 0x88..0x8B)
//   À Â Ä   → a        (second byte 0x80 / 0x82 / 0x84)
//   Î Ï     → i        (second byte 0x8E / 0x8F)
//   Ô Ö     → o        (second byte 0x94 / 0x96)
//   Ù Û Ü   → u        (second byte 0x99 / 0x9B / 0x9C)
//   Ç       → c        (second byte 0x87)
//
// Returns the number of source bytes consumed and writes 0..2 bytes to
// `dst`. `*written` is updated. If `dst_remaining < 2` and the fold
// would produce 2 bytes (only "œ"), we drop the char to avoid overflow.
static size_t fold_one_codepoint(const unsigned char *src, size_t src_len,
                                 char *dst, size_t dst_remaining,
                                 size_t *written) {
    *written = 0;
    if (src_len == 0) return 0;

    unsigned char b0 = src[0];

    // Pure ASCII fast path.
    if (b0 < 0x80) {
        if (dst_remaining >= 1) {
            dst[0] = (char) tolower((int) b0);
            *written = 1;
        }
        return 1;
    }

    // Two-byte UTF-8: 110xxxxx 10xxxxxx — anything else (3/4 byte) is
    // skipped (we don't expect emoji or non-Latin in FR transcripts).
    if ((b0 & 0xE0) == 0xC0 && src_len >= 2) {
        unsigned char b1 = src[1];

        if (b0 == 0xC3) {
            // Latin-1 supplement (most accented FR chars live here).
            char folded = '\0';
            switch (b1) {
            case 0xA9: case 0xA8: case 0xAA: case 0xAB:                  // éèêë
            case 0x89: case 0x88: case 0x8A: case 0x8B:                  // ÉÈÊË
                folded = 'e'; break;
            case 0xA0: case 0xA2: case 0xA4:                             // àâä
            case 0x80: case 0x82: case 0x84:                             // ÀÂÄ
                folded = 'a'; break;
            case 0xAE: case 0xAF:                                        // îï
            case 0x8E: case 0x8F:                                        // ÎÏ
                folded = 'i'; break;
            case 0xB4: case 0xB6:                                        // ôö
            case 0x94: case 0x96:                                        // ÔÖ
                folded = 'o'; break;
            case 0xB9: case 0xBB: case 0xBC:                             // ùûü
            case 0x99: case 0x9B: case 0x9C:                             // ÙÛÜ
                folded = 'u'; break;
            case 0xA7: case 0x87:                                        // çÇ
                folded = 'c'; break;
            default:
                // Unknown C3 codepoint — drop silently.
                break;
            }
            if (folded && dst_remaining >= 1) {
                dst[0] = folded;
                *written = 1;
            }
            return 2;
        }

        if (b0 == 0xC5 && b1 == 0x93) {
            // œ → oe (only 2-byte fold; needs 2 dst bytes).
            if (dst_remaining >= 2) {
                dst[0] = 'o';
                dst[1] = 'e';
                *written = 2;
            }
            return 2;
        }

        // Other 2-byte sequences: drop quietly.
        return 2;
    }

    // 3-byte (1110xxxx) — skip 3.
    if ((b0 & 0xF0) == 0xE0 && src_len >= 3) {
        return 3;
    }
    // 4-byte (11110xxx) — skip 4.
    if ((b0 & 0xF8) == 0xF0 && src_len >= 4) {
        return 4;
    }

    // Malformed continuation byte or truncated sequence — skip 1.
    return 1;
}

static void normalize_for_match(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;

    const unsigned char *u = (const unsigned char *) src;
    size_t src_len = strlen(src);
    size_t out = 0;

    while (src_len > 0 && out + 1 < cap) {  // reserve 1 for NUL
        size_t written = 0;
        size_t consumed = fold_one_codepoint(
            u, src_len, dst + out, cap - 1 - out, &written);
        if (consumed == 0) break;
        out += written;
        u += consumed;
        src_len -= consumed;
    }
    dst[out] = '\0';
}

static bool contains_keyword(const char *normalized) {
    if (!normalized || !*normalized) return false;
    for (size_t i = 0; i < HINT_KEYWORDS_COUNT; ++i) {
        if (strstr(normalized, HINT_KEYWORDS[i]) != NULL) {
            ESP_LOGI(TAG, "keyword hit: \"%s\"", HINT_KEYWORDS[i]);
            return true;
        }
    }
    return false;
}

// ── Hint result callback ────────────────────────────────────────────
//
// Slice 8 only logs the answer. TTS playback (sending the text back to
// the bridge for synthesis, or playing a pre-baked MP3) lands in slice 9.
static void on_hint_response(uint8_t puzzle_id, uint8_t level,
                             esp_err_t status, const char *text,
                             void *user_ctx) {
    (void) user_ctx;
    if (status != ESP_OK) {
        ESP_LOGW(TAG, "HINT failed (puzzle=%u level=%u): %s",
                 (unsigned) puzzle_id, (unsigned) level,
                 esp_err_to_name(status));
        return;
    }
    ESP_LOGI(TAG, "HINT received (puzzle=%u level=%u): %s",
             (unsigned) puzzle_id, (unsigned) level,
             text ? text : "(empty)");
}

// ── Public API ──────────────────────────────────────────────────────

esp_err_t voice_dispatcher_init(void) {
    if (s_initialized) return ESP_OK;
    s_initialized = true;
    ESP_LOGI(TAG, "voice_dispatcher ready (%u FR hint keywords)",
             (unsigned) HINT_KEYWORDS_COUNT);
    return ESP_OK;
}

void voice_dispatcher_handle_stt(const char *text, bool final) {
    if (!s_initialized) {
        ESP_LOGW(TAG, "stt before init: dropping");
        return;
    }
    if (!text || !*text) return;

    if (!final) {
        // Interim transcript. Slice 8 only acts on finals.
        ESP_LOGD(TAG, "stt interim: \"%s\"", text);
        return;
    }

    // Normalize once into a stack buffer; matching uses substr scan.
    // 256 bytes covers ~120 chars of folded FR comfortably (NPC turns
    // are short by design).
    char folded[256];
    normalize_for_match(text, folded, sizeof(folded));
    ESP_LOGI(TAG, "stt final raw=\"%s\" folded=\"%s\"", text, folded);

    if (!contains_keyword(folded)) {
        ESP_LOGI(TAG, "no keyword match, deferring to LLM intent path");
        return;
    }

    esp_err_t err = npc_engine_request_hint(
        DISPATCHER_PLACEHOLDER_PUZZLE_ID,
        DISPATCHER_DEFAULT_HINT_LEVEL,
        on_hint_response,
        NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "npc_engine_request_hint: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "hint request dispatched (puzzle=%u level=%u)",
                 (unsigned) DISPATCHER_PLACEHOLDER_PUZZLE_ID,
                 (unsigned) DISPATCHER_DEFAULT_HINT_LEVEL);
    }
}

void voice_dispatcher_handle_intent(const char *text, const char *model) {
    if (!s_initialized) {
        ESP_LOGW(TAG, "intent before init: dropping");
        return;
    }
    ESP_LOGI(TAG, "INTENT received (model=%s): %s",
             model && *model ? model : "?",
             text  && *text  ? text  : "(empty)");

    // Best-effort acknowledgement cue. The cue id is a convention —
    // when no entry exists in the npc_engine cue table, the engine
    // tries the id as a literal media path; failures are logged but
    // non-fatal.
    esp_err_t err = npc_engine_trigger_cue("intent_ack");
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "intent_ack cue: %s", esp_err_to_name(err));
    }
}
