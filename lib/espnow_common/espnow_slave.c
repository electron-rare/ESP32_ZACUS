// espnow_slave.c — ESP-NOW slave implementation (shared by all V3 puzzle nodes)
// Requires Wi-Fi to be initialised (WIFI_MODE_STA, no AP connection needed).
#include "espnow_slave.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "espnow_slave";

#define RECV_QUEUE_LEN 8

typedef struct {
    uint8_t mac[6];
    uint8_t data[250];
    int     data_len;
} recv_item_t;

static QueueHandle_t         s_recv_queue;
static uint8_t               s_master_mac[6];
static uint8_t               s_puzzle_id;
static espnow_cmd_callback_t s_callback;

// ---------------------------------------------------------------------------
// Internal callbacks (called from Wi-Fi ISR context)
// ---------------------------------------------------------------------------

static void on_recv(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len)
{
    recv_item_t item;
    memcpy(item.mac, info->src_addr, 6);
    int copy_len = len < (int)sizeof(item.data) ? len : (int)sizeof(item.data);
    memcpy(item.data, data, copy_len);
    item.data_len = copy_len;
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

esp_err_t espnow_slave_init(const uint8_t master_mac[6], uint8_t puzzle_id)
{
    s_puzzle_id = puzzle_id;
    memcpy(s_master_mac, master_mac, 6);

    s_recv_queue = xQueueCreate(RECV_QUEUE_LEN, sizeof(recv_item_t));
    if (!s_recv_queue) {
        ESP_LOGE(TAG, "Failed to create recv queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    // Register master as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, master_mac, 6);
    peer.channel = 0;      // use current Wi-Fi channel
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "Puzzle P%d slave ready, master=" MACSTR,
             puzzle_id, MAC2STR(master_mac));
    return ESP_OK;
}

esp_err_t espnow_slave_notify_solved(const uint8_t code_fragment[4],
                                     uint32_t solve_time_ms)
{
    // Wire format: [MSG_TYPE(1)] [PUZZLE_ID(1)] [CODE(4)] [SOLVE_MS(4)]
    uint8_t buf[10];
    buf[0] = (uint8_t)MSG_PUZZLE_SOLVED;
    buf[1] = s_puzzle_id;
    memcpy(buf + 2, code_fragment, 4);
    buf[6] = (uint8_t)(solve_time_ms >> 24);
    buf[7] = (uint8_t)(solve_time_ms >> 16);
    buf[8] = (uint8_t)(solve_time_ms >>  8);
    buf[9] = (uint8_t)(solve_time_ms      );
    return esp_now_send(s_master_mac, buf, sizeof(buf));
}

esp_err_t espnow_slave_request_hint(void)
{
    uint8_t buf[2] = { (uint8_t)MSG_HINT_REQUEST, s_puzzle_id };
    return esp_now_send(s_master_mac, buf, sizeof(buf));
}

esp_err_t espnow_slave_send_status(const puzzle_status_t *status)
{
    uint8_t buf[1 + sizeof(puzzle_status_t)];
    buf[0] = (uint8_t)MSG_STATUS_REPLY;
    memcpy(buf + 1, status, sizeof(puzzle_status_t));
    return esp_now_send(s_master_mac, buf, sizeof(buf));
}

void espnow_slave_register_callback(espnow_cmd_callback_t cb)
{
    s_callback = cb;
}

void espnow_slave_process(void)
{
    recv_item_t item;
    while (xQueueReceive(s_recv_queue, &item, 0) == pdTRUE) {
        if (item.data_len < 1) continue;
        espnow_msg_type_t type = (espnow_msg_type_t)item.data[0];
        if (s_callback) {
            s_callback(type,
                       item.data + 1,
                       (size_t)(item.data_len - 1));
        }
    }
}
