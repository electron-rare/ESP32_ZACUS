// audio_ble_control.cpp — BLE/BT AVRCP control for Bluetooth speaker from BOX-3
// BOX-3 (ESP32-S3) acts as Bluetooth classic controller (A2DP source + AVRCP CT).
// Sends AVRCP passthrough commands: play, pause, stop, next, volume.
//
// Dependencies: esp_bt, esp_a2dp_api, esp_avrc_api (ESP-IDF classic BT stack)
// Note: ESP32-S3-BOX-3 has classic BT (not BLE-only), so A2DP + AVRCP are available.

#include "audio/audio_ble_control.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_avrc_api.h"
#include "esp_a2dp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

static const char *TAG = "AUDIO_BT";

// Track enum — must match pre-loaded tracks on the Bluetooth speaker / media player
typedef enum {
    TRACK_LAB_AMBIANCE = 0,  // background loop during intro/profiling
    TRACK_TENSION      = 1,  // climax phase
    TRACK_VICTORY      = 2,  // game won
    TRACK_FAILURE      = 3,  // game over / timeout
    TRACK_THINKING     = 4,  // adaptive phase background
    TRACK_TRANSITION   = 5,  // puzzle-to-puzzle transition stinger
} audio_track_t;

static bool          s_bt_connected  = false;
static uint8_t       s_peer_bda[6]   = {0};
static uint8_t       s_current_track = 0xFF;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static esp_err_t avrc_passthrough(esp_avrc_pt_cmd_t cmd)
{
    if (!s_bt_connected) {
        ESP_LOGW(TAG, "BT speaker not connected — command 0x%02x dropped", cmd);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_avrc_ct_send_passthrough_cmd(
        0, cmd, ESP_AVRC_PT_CMD_STATE_PUSHED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP passthrough 0x%02x failed: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// AVRC event callback
// ─────────────────────────────────────────────────────────────────────────────

static void avrc_ct_callback(esp_avrc_ct_cb_event_t event,
                              esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        if (param->conn_stat.connected) {
            ESP_LOGI(TAG, "AVRCP connected");
        } else {
            ESP_LOGI(TAG, "AVRCP disconnected");
        }
        break;
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        ESP_LOGD(TAG, "AVRCP passthrough RSP: key_code=0x%02x, key_state=%d",
                 param->psth_rsp.key_code, param->psth_rsp.key_state);
        break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        ESP_LOGD(TAG, "AVRCP notify event_id=0x%02x", param->change_ntf.event_id);
        break;
    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// A2DP source callback
// ─────────────────────────────────────────────────────────────────────────────

static void a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            s_bt_connected = true;
            memcpy(s_peer_bda, param->conn_stat.remote_bda, sizeof(s_peer_bda));
            ESP_LOGI(TAG, "A2DP connected to %02x:%02x:%02x:%02x:%02x:%02x",
                     s_peer_bda[0], s_peer_bda[1], s_peer_bda[2],
                     s_peer_bda[3], s_peer_bda[4], s_peer_bda[5]);
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_bt_connected = false;
            ESP_LOGW(TAG, "A2DP disconnected");
        }
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
        break;
    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void audio_ble_init(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth audio control (A2DP + AVRCP)");

    // Classic BT controller config
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // A2DP source (BOX-3 as audio sender, not relevant for volume/track control
    // but required to establish the BT profile stack for AVRCP)
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2dp_cb));
    ESP_ERROR_CHECK(esp_a2d_source_init());

    // AVRCP controller — sends play/stop/volume to speaker
    ESP_ERROR_CHECK(esp_avrc_ct_init());
    ESP_ERROR_CHECK(esp_avrc_ct_register_callback(avrc_ct_callback));

    // Enable absolute volume
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set,
                                        ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_ERROR_CHECK(esp_avrc_ct_send_register_notification_cmd(
        0, ESP_AVRC_RN_VOLUME_CHANGE, 0));

    ESP_LOGI(TAG, "BT audio stack ready — waiting for speaker pairing");
}

bool audio_ble_is_connected(void)
{
    return s_bt_connected;
}

esp_err_t audio_play(void)
{
    return avrc_passthrough(ESP_AVRC_PT_CMD_PLAY);
}

esp_err_t audio_pause(void)
{
    return avrc_passthrough(ESP_AVRC_PT_CMD_PAUSE);
}

esp_err_t audio_stop(void)
{
    return avrc_passthrough(ESP_AVRC_PT_CMD_STOP);
}

esp_err_t audio_next_track(void)
{
    return avrc_passthrough(ESP_AVRC_PT_CMD_FORWARD);
}

esp_err_t audio_prev_track(void)
{
    return avrc_passthrough(ESP_AVRC_PT_CMD_BACKWARD);
}

esp_err_t audio_set_volume(uint8_t volume_pct)
{
    if (!s_bt_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    // 0-100% → 0-127 (AVRCP absolute volume range)
    uint8_t avrc_vol = (uint8_t)((volume_pct * 127UL) / 100UL);
    ESP_LOGI(TAG, "Set volume: %d%% → AVRC %d", volume_pct, avrc_vol);
    return esp_avrc_ct_send_set_absolute_volume_cmd(0, avrc_vol);
}

// Game phase → audio track mapping
// Assumes the Bluetooth speaker playlist is loaded in the order of audio_track_t.
// Navigation uses NEXT/PREV commands relative to current track position.
void audio_play_for_phase(game_phase_t phase)
{
    audio_track_t target;

    switch (phase) {
    case GAME_INTRO:
    case GAME_PROFILING:
        target = TRACK_LAB_AMBIANCE;
        break;
    case GAME_ADAPTIVE:
        target = TRACK_THINKING;
        break;
    case GAME_CLIMAX:
        target = TRACK_TENSION;
        break;
    case GAME_OUTRO:
        target = TRACK_VICTORY;
        break;
    default:
        return;
    }

    // Navigate to target track by sending NEXT until we reach the correct index.
    // Simplified approach: assumes playlist starts at TRACK_LAB_AMBIANCE (index 0).
    if (s_current_track == target) {
        audio_play();
        return;
    }

    // Skip forward to target (wraps at TRACK_TRANSITION → TRACK_LAB_AMBIANCE)
    uint8_t steps = (target > s_current_track)
                        ? (target - s_current_track)
                        : (6 - s_current_track + target);

    for (uint8_t i = 0; i < steps; i++) {
        audio_next_track();
        vTaskDelay(pdMS_TO_TICKS(200));  // brief delay between AVRCP commands
    }
    s_current_track = target;
    audio_play();
    ESP_LOGI(TAG, "Phase %d → track %d", phase, target);
}
