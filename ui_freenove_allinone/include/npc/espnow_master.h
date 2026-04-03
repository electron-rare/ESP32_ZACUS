// espnow_master.h — BOX-3 ESP-NOW master (Arduino/C++ wrapper)
// Orchestrates 7 puzzle slave nodes, collects results, assembles final code.
// This header wraps the C implementation for the Arduino/C++ BOX-3 codebase.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Shared message types (mirrors espnow_slave.h — must stay in sync)
// ---------------------------------------------------------------------------
typedef enum {
    MSG_PUZZLE_SOLVED   = 0x01,
    MSG_PUZZLE_RESET    = 0x02,
    MSG_PUZZLE_CONFIG   = 0x03,
    MSG_HINT_REQUEST    = 0x04,
    MSG_STATUS_POLL     = 0x05,
    MSG_STATUS_REPLY    = 0x06,
    MSG_LED_COMMAND     = 0x07,
    MSG_HEARTBEAT       = 0x08,
} espnow_msg_type_t;

typedef enum {
    DIFFICULTY_EASY   = 0,
    DIFFICULTY_NORMAL = 1,
    DIFFICULTY_HARD   = 2,
} puzzle_difficulty_t;

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
typedef enum {
    ESPNOW_EV_PUZZLE_SOLVED  = 1,
    ESPNOW_EV_HINT_REQUEST   = 2,
    ESPNOW_EV_HEARTBEAT      = 3,
    ESPNOW_EV_NODE_TIMEOUT   = 4,
} espnow_event_type_t;

typedef struct {
    espnow_event_type_t type;
    uint8_t             puzzle_id;
    uint8_t             code_fragment[4];
    uint32_t            solve_time_ms;
} espnow_event_t;

typedef void (*espnow_event_cb_t)(const espnow_event_t *ev);

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
esp_err_t espnow_master_init(espnow_event_cb_t cb);
esp_err_t espnow_master_reset_puzzle(uint8_t puzzle_id);
esp_err_t espnow_master_configure_puzzle(uint8_t puzzle_id,
                                          puzzle_difficulty_t difficulty);
esp_err_t espnow_master_send_final_code(const char code[9]);
void      espnow_master_process(void);
bool      espnow_master_all_solved(const uint8_t puzzle_ids[], uint8_t count);
void      espnow_master_assemble_code(char code_out[9]);

#ifdef __cplusplus
}
#endif
