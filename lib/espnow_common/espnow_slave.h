// espnow_slave.h — Common ESP-NOW slave for all V3 puzzle nodes
// All puzzle ESP32s include this header and link espnow_slave.c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ---------------------------------------------------------------------------
// Message types (must match BOX-3 master definitions in espnow_master.h)
// ---------------------------------------------------------------------------
typedef enum {
    MSG_PUZZLE_SOLVED   = 0x01,   // puzzle → master: puzzle complete, code fragment attached
    MSG_PUZZLE_RESET    = 0x02,   // master → puzzle: reset to initial state
    MSG_PUZZLE_CONFIG   = 0x03,   // master → puzzle: set difficulty variant
    MSG_HINT_REQUEST    = 0x04,   // puzzle → master: player requested hint (phone hook)
    MSG_STATUS_POLL     = 0x05,   // master → puzzle: request status update
    MSG_STATUS_REPLY    = 0x06,   // puzzle → master: current state, elapsed time
    MSG_LED_COMMAND     = 0x07,   // master → puzzle: set LED color/pattern
    MSG_HEARTBEAT       = 0x08,   // both directions: keep-alive every 5 s
} espnow_msg_type_t;

// ---------------------------------------------------------------------------
// Difficulty variants
// ---------------------------------------------------------------------------
typedef enum {
    DIFFICULTY_EASY   = 0,   // NON_TECH group: shorter sequences, visual aids
    DIFFICULTY_NORMAL = 1,   // MIXED group: default parameters
    DIFFICULTY_HARD   = 2,   // TECH group: longer / harder variant
} puzzle_difficulty_t;

// ---------------------------------------------------------------------------
// Puzzle status (sent in MSG_STATUS_REPLY)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  puzzle_id;         // P1..P7 (1-7)
    uint8_t  state;             // 0=idle, 1=active, 2=solved, 3=failed
    uint8_t  attempts;          // number of wrong attempts since reset
    uint8_t  hints_used;        // hints consumed since reset
    uint32_t elapsed_ms;        // time since puzzle activated (ms)
    uint8_t  code_fragment[4];  // code digits contributed by this puzzle
    uint8_t  difficulty;        // current difficulty variant
} puzzle_status_t;

// ---------------------------------------------------------------------------
// Payload structures
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  puzzle_id;
    uint8_t  code_fragment[4];
    uint32_t solve_time_ms;
} puzzle_solved_payload_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialize ESP-NOW in slave mode.
// master_mac: MAC address of BOX-3 master. Use {0xFF,...} for broadcast.
// puzzle_id:  P1..P7 (1-7)
esp_err_t espnow_slave_init(const uint8_t master_mac[6], uint8_t puzzle_id);

// Notify master that puzzle is solved with code fragment.
esp_err_t espnow_slave_notify_solved(const uint8_t code_fragment[4],
                                     uint32_t solve_time_ms);

// Request a hint from master (triggers NPC voice line).
esp_err_t espnow_slave_request_hint(void);

// Send status reply (call from MSG_STATUS_POLL handler).
esp_err_t espnow_slave_send_status(const puzzle_status_t *status);

// Register callback for incoming master commands.
typedef void (*espnow_cmd_callback_t)(espnow_msg_type_t type,
                                      const uint8_t *payload,
                                      size_t len);
void espnow_slave_register_callback(espnow_cmd_callback_t cb);

// Process inbound ESP-NOW queue — call from main FreeRTOS task loop.
void espnow_slave_process(void);
