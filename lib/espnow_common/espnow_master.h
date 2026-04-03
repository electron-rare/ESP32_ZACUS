// espnow_master.h — BOX-3 ESP-NOW master: orchestrates 7 puzzle slave nodes
// Broadcasts commands, collects results, assembles final 8-digit code.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "espnow_slave.h"   // shared message types + puzzle_difficulty_t

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Events emitted to the application layer
// ---------------------------------------------------------------------------
typedef enum {
    ESPNOW_EV_PUZZLE_SOLVED  = 1,
    ESPNOW_EV_HINT_REQUEST   = 2,
    ESPNOW_EV_HEARTBEAT      = 3,
    ESPNOW_EV_NODE_TIMEOUT   = 4,   // no heartbeat for > 30 s
} espnow_event_type_t;

typedef struct {
    espnow_event_type_t type;
    uint8_t             puzzle_id;       // 1..7
    uint8_t             code_fragment[4];
    uint32_t            solve_time_ms;
} espnow_event_t;

typedef void (*espnow_event_cb_t)(const espnow_event_t *ev);

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialize ESP-NOW master mode. cb is called on every puzzle event.
esp_err_t espnow_master_init(espnow_event_cb_t cb);

// Send MSG_PUZZLE_RESET to a specific puzzle (broadcast-addressed).
esp_err_t espnow_master_reset_puzzle(uint8_t puzzle_id);

// Send MSG_PUZZLE_CONFIG (difficulty) to a specific puzzle.
esp_err_t espnow_master_configure_puzzle(uint8_t puzzle_id,
                                          puzzle_difficulty_t difficulty);

// Send the 8-digit final code to P7 Coffre via MSG_PUZZLE_CONFIG.
// code must be a null-terminated string of exactly 8 digit characters.
esp_err_t espnow_master_send_final_code(const char code[9]);

// Process inbound ESP-NOW queue — call from main FreeRTOS task loop.
void espnow_master_process(void);

// Return true if all puzzle_ids in the array have reported solved.
bool espnow_master_all_solved(const uint8_t puzzle_ids[], uint8_t count);

// Assemble the 8-digit final code from all registered solved fragments.
// Code layout: P1[0,1] | P2[2] | P4[3] | P5[4] | P6[5,6] | P3[7]
// code_out must be at least 9 bytes (8 digits + null terminator).
void espnow_master_assemble_code(char code_out[9]);

#ifdef __cplusplus
}
#endif
