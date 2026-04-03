// espnow_master.c — BOX-3 ESP-NOW master implementation
// Runs on the Freenove ESP32-S3 BOX-3 (orchestrator node).
#include "espnow_master.h"
#include "esp_now.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "espnow_master";

// Broadcast MAC — puzzles listen for commands addressed here
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Internal state per puzzle node
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  puzzle_id;
    uint8_t  mac[6];
    bool     registered;
    bool     solved;
    uint8_t  code_fragment[4];
    uint32_t solve_time_ms;
    uint32_t last_heartbeat_ms;
} puzzle_node_t;

static puzzle_node_t    s_nodes[8];   // index = puzzle_id (1-7, index 0 unused)
static QueueHandle_t    s_recv_queue;
static espnow_event_cb_t s_event_cb;

typedef struct {
    uint8_t src_mac[6];
    uint8_t data[250];
    int     len;
} master_recv_item_t;

// ---------------------------------------------------------------------------
// Internal callbacks (Wi-Fi ISR context)
// ---------------------------------------------------------------------------

static void on_recv(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len)
{
    master_recv_item_t item;
    memcpy(item.src_mac, info->src_addr, 6);
    int copy = len < (int)sizeof(item.data) ? len : (int)sizeof(item.data);
    memcpy(item.data, data, copy);
    item.len = copy;
    xQueueSendFromISR(s_recv_queue, &item, NULL);
}

static void on_sent(const uint8_t *mac, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed to " MACSTR, MAC2STR(mac));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t espnow_master_init(espnow_event_cb_t cb)
{
    s_event_cb = cb;
    s_recv_queue = xQueueCreate(16, sizeof(master_recv_item_t));
    if (!s_recv_queue) {
        ESP_LOGE(TAG, "Failed to create recv queue");
        return ESP_ERR_NO_MEM;
    }
    memset(s_nodes, 0, sizeof(s_nodes));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    // Register broadcast peer (for addressing all slaves at once)
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, kBroadcast, 6);
    bcast.channel = 0;
    bcast.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&bcast));

    ESP_LOGI(TAG, "Master initialized");
    return ESP_OK;
}

esp_err_t espnow_master_reset_puzzle(uint8_t puzzle_id)
{
    if (puzzle_id < 1 || puzzle_id > 7) return ESP_ERR_INVALID_ARG;
    uint8_t buf[2] = { (uint8_t)MSG_PUZZLE_RESET, puzzle_id };
    return esp_now_send(kBroadcast, buf, sizeof(buf));
}

esp_err_t espnow_master_configure_puzzle(uint8_t puzzle_id,
                                          puzzle_difficulty_t difficulty)
{
    if (puzzle_id < 1 || puzzle_id > 7) return ESP_ERR_INVALID_ARG;
    uint8_t buf[3] = { (uint8_t)MSG_PUZZLE_CONFIG, puzzle_id,
                       (uint8_t)difficulty };
    return esp_now_send(kBroadcast, buf, sizeof(buf));
}

esp_err_t espnow_master_send_final_code(const char code[9])
{
    // Wire format: [MSG_PUZZLE_CONFIG(1)] [P7_ID=7(1)] [CODE(8)]
    uint8_t buf[10];
    buf[0] = (uint8_t)MSG_PUZZLE_CONFIG;
    buf[1] = 7;  // P7 puzzle ID
    memcpy(buf + 2, code, 8);
    return esp_now_send(kBroadcast, buf, sizeof(buf));
}

void espnow_master_process(void)
{
    master_recv_item_t item;
    while (xQueueReceive(s_recv_queue, &item, 0) == pdTRUE) {
        if (item.len < 2) continue;

        espnow_msg_type_t type = (espnow_msg_type_t)item.data[0];
        uint8_t puzzle_id = item.data[1];
        if (puzzle_id < 1 || puzzle_id > 7) continue;

        puzzle_node_t *node = &s_nodes[puzzle_id];
        node->registered = true;
        memcpy(node->mac, item.src_mac, 6);
        node->last_heartbeat_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (type == MSG_PUZZLE_SOLVED && item.len >= 10) {
            node->solved = true;
            memcpy(node->code_fragment, item.data + 2, 4);
            // Bytes 6-9: big-endian solve_time_ms
            node->solve_time_ms = ((uint32_t)item.data[6] << 24)
                                | ((uint32_t)item.data[7] << 16)
                                | ((uint32_t)item.data[8] <<  8)
                                | ((uint32_t)item.data[9]      );

            ESP_LOGI(TAG, "P%d SOLVED code=%d%d%d%d time=%lu ms",
                     puzzle_id,
                     node->code_fragment[0], node->code_fragment[1],
                     node->code_fragment[2], node->code_fragment[3],
                     node->solve_time_ms);

            if (s_event_cb) {
                espnow_event_t ev = {
                    .type          = ESPNOW_EV_PUZZLE_SOLVED,
                    .puzzle_id     = puzzle_id,
                    .solve_time_ms = node->solve_time_ms,
                };
                memcpy(ev.code_fragment, node->code_fragment, 4);
                s_event_cb(&ev);
            }

        } else if (type == MSG_HINT_REQUEST) {
            ESP_LOGI(TAG, "Hint request from P%d", puzzle_id);
            if (s_event_cb) {
                espnow_event_t ev = {
                    .type      = ESPNOW_EV_HINT_REQUEST,
                    .puzzle_id = puzzle_id,
                };
                s_event_cb(&ev);
            }

        } else if (type == MSG_HEARTBEAT) {
            ESP_LOGD(TAG, "Heartbeat P%d", puzzle_id);
            if (s_event_cb) {
                espnow_event_t ev = {
                    .type      = ESPNOW_EV_HEARTBEAT,
                    .puzzle_id = puzzle_id,
                };
                s_event_cb(&ev);
            }
        }
    }
}

bool espnow_master_all_solved(const uint8_t puzzle_ids[], uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = puzzle_ids[i];
        if (id < 1 || id > 7 || !s_nodes[id].solved) return false;
    }
    return true;
}

void espnow_master_assemble_code(char code_out[9])
{
    // Digit layout per scenario YAML:
    //   P1 → digits 0,1   (code_fragment[0], [1])
    //   P2 → digit  2     (code_fragment[0])
    //   P4 → digit  3     (code_fragment[0])
    //   P5 → digit  4     (code_fragment[0])
    //   P6 → digits 5,6   (code_fragment[0], [1])
    //   P3 → digit  7     (code_fragment[0])
    static const uint8_t kCodeMap[8][2] = {
        {1, 0}, {1, 1},  // P1 → digits 0,1
        {2, 0},          // P2 → digit 2
        {4, 0},          // P4 → digit 3
        {5, 0},          // P5 → digit 4
        {6, 0}, {6, 1},  // P6 → digits 5,6
        {3, 0},          // P3 → digit 7
    };

    for (int i = 0; i < 8; i++) {
        uint8_t pid      = kCodeMap[i][0];
        uint8_t frag_idx = kCodeMap[i][1];
        code_out[i] = '0' + (s_nodes[pid].code_fragment[frag_idx] % 10);
    }
    code_out[8] = '\0';

    ESP_LOGI(TAG, "Final code assembled: %s", code_out);
}
