// Zacus master — ESP-IDF entry point (P1 slice 2).
//
// Responsibilities at this slice:
//   1. Initialize NVS (required by Wi-Fi).
//   2. Initialize esp_netif + default event loop.
//   3. Read Wi-Fi creds from NVS namespace "wifi" (keys "ssid" / "pwd").
//        - If creds present  : start STA, wait for IP_EVENT_STA_GOT_IP.
//        - If creds absent   : fall back to open AP "zacus-setup" so the
//                              operator can still reach the OTA endpoint
//                              (and provision creds in a later slice).
//   4. Once the network is up, call ota_server_init() so the inherited
//      HTTP server (port 80) starts answering /version, /status, /ota.
//   5. Mount the LittleFS "storage" partition on /littlefs and list it.
//   6. Log heap stats + idle loop with periodic heartbeat (60 s).
//
// Subsequent slices port the NPC engine, voice pipeline, media manager, etc.
// See docs/superpowers/specs/2026-05-03-voice-pipeline-esp-sr-design.md.

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "ota_server.h"
#include "media_manager.h"
#include "npc_engine.h"
#include "hints_client.h"
#include "voice_pipeline.h"

// Hints engine endpoint (slice 5). Hardcoded for now — slice 7 will move
// this to NVS so the field operator can repoint the firmware without a flash.
#define ZACUS_HINTS_BASE_URL  "http://192.168.0.150:8302"

static const char *TAG = "zacus_main";

// Soft-AP fallback when no creds in NVS yet.
#define ZACUS_FALLBACK_AP_SSID  "zacus-setup"
#define ZACUS_FALLBACK_AP_CHAN  6
#define ZACUS_STA_MAX_RETRY     8

// ─── Wi-Fi state ─────────────────────────────────────────────────────────────
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_sta_retry = 0;

// ─── ota_server externs (real implementations come with the puzzle/master
//     state in a later P1 slice; for now we provide trivial stubs so the
//     component links cleanly). ─────────────────────────────────────────────
int puzzle_get_battery_pct(void) {
    return 100;
}

int puzzle_get_espnow_peer_count(void) {
    return 0;
}

// Slice 6: wake-word callback. Runs on the voice_pipeline capture task,
// keep it short. The pipeline already auto-transitioned to LISTENING
// before invoking us; here we just log + ensure capture is running so
// downstream STT (slice 7) has audio to consume.
static void on_voice_wake(const char *wake_word, void *user_ctx) {
    (void) user_ctx;
    ESP_LOGI(TAG, "WAKE: \"%s\" detected, transitioning to LISTENING",
             wake_word ? wake_word : "(null)");
    // Capture is already running (esp-sr feeds it), but if a future
    // slice toggles it off between wakes, this keeps us robust.
    esp_err_t err = voice_pipeline_start_capture();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "voice_pipeline_start_capture from wake cb: %s",
                 esp_err_to_name(err));
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void log_heap_stats(const char *phase) {
    ESP_LOGI(TAG, "[heap @ %s] free=%u internal=%u psram=%u",
             phase,
             (unsigned) esp_get_free_heap_size(),
             (unsigned) esp_get_free_internal_heap_size(),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static esp_err_t mount_littlefs(void) {
    const esp_vfs_littlefs_conf_t conf = {
        .base_path        = "/littlefs",
        .partition_label  = "storage",
        .format_if_mount_failed = true,
        .dont_mount       = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted at %s — %u / %u bytes used",
                 conf.base_path, (unsigned) used, (unsigned) total);
    }
    return ESP_OK;
}

static void list_littlefs_root(void) {
    DIR *dir = opendir("/littlefs");
    if (!dir) {
        ESP_LOGW(TAG, "opendir(/littlefs) failed");
        return;
    }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  /littlefs/%s (type=%d)", ent->d_name, ent->d_type);
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "LittleFS root contains %d entries", count);
}

// ─── Wi-Fi: NVS creds + event handler ────────────────────────────────────────

// Reads NVS namespace "wifi" keys "ssid" + "pwd". Returns ESP_OK if both
// keys are present and ssid is non-empty. Buffers are NUL-terminated.
static esp_err_t load_wifi_creds(char *ssid, size_t ssid_len,
                                 char *pwd,  size_t pwd_len) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace 'wifi' not found (%s)", esp_err_to_name(err));
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_str(h, "ssid", ssid, &len);
    if (err != ESP_OK || len <= 1) {
        ESP_LOGI(TAG, "NVS 'wifi/ssid' missing (%s)", esp_err_to_name(err));
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    len = pwd_len;
    err = nvs_get_str(h, "pwd", pwd, &len);
    if (err != ESP_OK) {
        // Empty password is acceptable (open network).
        pwd[0] = '\0';
    }
    nvs_close(h);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start — connecting…");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_sta_retry < ZACUS_STA_MAX_RETRY) {
            s_sta_retry++;
            ESP_LOGW(TAG, "STA disconnected — retry %d/%d", s_sta_retry, ZACUS_STA_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "STA give up after %d retries", ZACUS_STA_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) data;
        ESP_LOGI(TAG, "STA got IP " IPSTR, IP2STR(&event->ip_info.ip));
        s_sta_retry = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP started — SSID=\"%s\" (open)", ZACUS_FALLBACK_AP_SSID);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "AP: client joined");
    }
}

// Returns true if STA connected, false if AP fallback (or STA gave up).
static bool wifi_bring_up(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    char ssid[33] = {0};
    char pwd[65]  = {0};
    bool have_creds = (load_wifi_creds(ssid, sizeof(ssid), pwd, sizeof(pwd)) == ESP_OK);

    if (have_creds) {
        ESP_LOGI(TAG, "Wi-Fi: STA mode (ssid=\"%s\")", ssid);
        esp_netif_create_default_wifi_sta();

        wifi_config_t wc = {0};
        strncpy((char *) wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
        strncpy((char *) wc.sta.password, pwd,  sizeof(wc.sta.password) - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;  // accept any; we don't pin

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK(esp_wifi_start());

        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(20000));

        if (bits & WIFI_CONNECTED_BIT) {
            return true;
        }
        ESP_LOGW(TAG, "STA failed within 20 s — fallback to AP");
        esp_wifi_stop();
    } else {
        ESP_LOGI(TAG, "Wi-Fi: no creds in NVS, starting AP fallback");
    }

    // AP fallback (open network — provisioning will be added later).
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {0};
    strncpy((char *) ap_cfg.ap.ssid, ZACUS_FALLBACK_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = strlen(ZACUS_FALLBACK_AP_SSID);
    ap_cfg.ap.channel        = ZACUS_FALLBACK_AP_CHAN;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return false;
}

// ─── app_main ────────────────────────────────────────────────────────────────

void app_main(void) {
    ESP_LOGI(TAG, "Zacus master booting (ESP-IDF scaffold, P1 slice 2)");

    log_heap_stats("boot");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS initialized");

    bool sta_ok = wifi_bring_up();
    ESP_LOGI(TAG, "Wi-Fi up (mode=%s)", sta_ok ? "STA" : "AP-fallback");

    esp_err_t ota_err = ota_server_init();
    if (ota_err != ESP_OK) {
        ESP_LOGE(TAG, "ota_server_init failed: %s", esp_err_to_name(ota_err));
    } else {
        ESP_LOGI(TAG, "OTA server listening on :%d", OTA_SERVER_PORT);
    }

    if (mount_littlefs() == ESP_OK) {
        list_littlefs_root();

        // Slice 3: bring up the ported media_manager. Catalog dirs live on
        // LittleFS so this must run *after* the mount succeeds.
        media_manager_config_t media_cfg;
        media_manager_default_config(&media_cfg);
        esp_err_t media_err = media_manager_init(&media_cfg);
        if (media_err != ESP_OK) {
            ESP_LOGE(TAG, "media_manager_init failed: %s",
                     esp_err_to_name(media_err));
        } else {
            // Smoke test: try to play /littlefs/intro.mp3. The file is
            // unlikely to exist this early — that's fine, the manager
            // returns ESP_ERR_NOT_FOUND and logs a warning, no crash.
            esp_err_t play_err = media_manager_play("/littlefs/intro.mp3");
            ESP_LOGI(TAG, "media smoke play -> %s",
                     esp_err_to_name(play_err));

            // Slice 4: bring up the ported npc_engine. Cue table is empty
            // at this stage — wiring the scenario IR-driven cue catalog is
            // a follow-up slice. The engine still boots, accepts ticks
            // (no-op when auto_evaluate is false), and is ready to receive
            // trigger_cue calls from REST/diagnostic surfaces.
            const npc_engine_config_t npc_cfg = {
                .cues                = NULL,
                .cue_count           = 0,
                .auto_evaluate       = false,
                .auto_play_decisions = false,
            };
            esp_err_t npc_err = npc_engine_init(&npc_cfg);
            if (npc_err != ESP_OK) {
                ESP_LOGE(TAG, "npc_engine_init failed: %s",
                         esp_err_to_name(npc_err));
            }

            // Slice 5: bring up the hints HTTP client (so npc_engine can
            // route hint requests through the real backend) and the voice
            // pipeline (I2S capture stub + state machine, no auto-start).
            esp_err_t hints_err = hints_client_init(ZACUS_HINTS_BASE_URL);
            if (hints_err != ESP_OK) {
                ESP_LOGW(TAG, "hints_client_init failed: %s — npc will use stub",
                         esp_err_to_name(hints_err));
            }

            voice_pipeline_config_t voice_cfg;
            voice_pipeline_default_config(&voice_cfg);
            // Slice 6: bring up esp-sr AFE + WakeNet (placeholder
            // wn9_hiesp). Auto-start capture so the wake detector is
            // hot from boot — saying "Hi ESP" fires the callback below.
            voice_cfg.enable_wake_word   = true;
            voice_cfg.auto_start_capture = true;
            voice_pipeline_set_wake_callback(on_voice_wake, NULL);
            esp_err_t voice_err = voice_pipeline_init(&voice_cfg);
            if (voice_err != ESP_OK) {
                ESP_LOGW(TAG, "voice_pipeline_init failed: %s",
                         esp_err_to_name(voice_err));
            } else if (voice_pipeline_wake_word_active()) {
                ESP_LOGI(TAG, "voice: wake-word detector active "
                              "(placeholder \"hi esp\")");
            } else {
                ESP_LOGW(TAG, "voice: wake-word inactive — running "
                              "in slice-5 stub mode");
            }
        }
    }

    log_heap_stats("post-init");

    // Mark this firmware valid only after subsystems came up cleanly.
    ota_server_mark_valid();

    ESP_LOGI(TAG, "entering idle loop (heartbeat every 60 s)");
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        tick++;
        const uint32_t uptime_ms = (uint32_t) esp_log_timestamp();
        ESP_LOGI(TAG, "heartbeat #%u — uptime=%llu s",
                 (unsigned) tick,
                 (unsigned long long) (uptime_ms / 1000));
        // Drive the slice-3/4 subsystems from the heartbeat. Once we have
        // a real game loop these will move to a dedicated FreeRTOS task
        // running at ~5 Hz; for now 60 s is enough to keep mood + media
        // simulation state coherent without spamming the log.
        media_manager_update(uptime_ms);
        npc_engine_update(uptime_ms);
    }
}
