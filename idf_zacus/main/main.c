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

#include "mdns.h"

#include "ota_server.h"
#include "media_manager.h"
#include "npc_engine.h"
#include "hints_client.h"
#include "voice_pipeline.h"
#include "voice_dispatcher.h"
#include "voice_hook_endpoint.h"
#include "game_endpoint.h"

// Hints engine endpoint (slice 5). Hardcoded for now — slice 7 will move
// this to NVS so the field operator can repoint the firmware without a flash.
#define ZACUS_HINTS_BASE_URL  "http://192.168.0.150:8302"

// Slice 7: voice-bridge WebSocket on the MacStudio (Tailscale address).
// Hardcoded here for the same reason as ZACUS_HINTS_BASE_URL — moves to
// NVS in a follow-up slice. The bridge endpoint is documented in
// docs/superpowers/specs/2026-05-03-voice-pipeline-esp-sr-design.md.
#define ZACUS_VOICE_BRIDGE_WS_URL  "ws://100.116.92.12:8200/voice/ws"

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

// Slice 7/8: STT callback. Runs on the WebSocket event-loop task —
// keep it short. The actual routing (keyword fast-path → hints engine,
// non-keyword → defer to LLM intent path) is owned by voice_dispatcher,
// which voice_pipeline_ws calls in parallel with this user callback.
// We keep the log here as a diagnostic breadcrumb for field debugging.
static void on_voice_stt(const char *text, bool final, void *user_ctx) {
    (void) user_ctx;
    ESP_LOGI(TAG, "STT(final=%d): %s", final ? 1 : 0,
             text ? text : "(null)");
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

// ─── mDNS bring-up (slice 12) ────────────────────────────────────────────────
//
// Publishes `zacus-master.local` once Wi-Fi STA is up so PLIP and the
// dashboard can discover the master without a DHCP reservation. We
// also advertise a `_zacus._tcp` service on port 80 with TXT records
// pointing at the voice-hook URI — useful for `dns-sd -B _zacus._tcp`
// style introspection from the workshop laptop.
//
// In AP-fallback mode we deliberately skip mDNS: there is no upstream
// resolver to claim the hostname against, and several tooling stacks
// (avahi, bonjour) trip over a duplicate-name race when the AP later
// goes back to STA. The PLIP fallback in that scenario is the static
// AP IP (192.168.4.1).

static void start_mdns(void) {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set("zacus-master");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_hostname_set: %s", esp_err_to_name(err));
        // Continue — the daemon is up, just no hostname claim.
    }

    err = mdns_instance_name_set("Zacus Master ESP32-S3");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_instance_name_set: %s", esp_err_to_name(err));
    }

    // Advertise the master HTTP surface as a `_zacus._tcp` service on
    // port 80. The TXT records let PLIP firmware confirm it found the
    // right device + which voice-hook path to POST to (so a future
    // protocol bump can be discovered without reflashing PLIP).
    err = mdns_service_add(NULL, "_zacus", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add(_zacus._tcp:80): %s",
                 esp_err_to_name(err));
        return;
    }

    (void) mdns_service_instance_name_set("_zacus", "_tcp",
                                          "Zacus Master Voice Hook");

    mdns_txt_item_t txt[] = {
        {"path",    "/voice/hook"},
        {"version", "1"},
    };
    err = mdns_service_txt_set("_zacus", "_tcp", txt,
                               sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_txt_set: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "mDNS up — hostname=zacus-master.local, "
                  "service=_zacus._tcp:80");
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

    // Slice 12: publish zacus-master.local once we're on a real LAN.
    // Skip in AP-fallback to avoid hostname-claim races when STA later
    // recovers (and because there is no upstream resolver anyway).
    if (sta_ok) {
        start_mdns();
    } else {
        ESP_LOGW(TAG, "mDNS not started in AP mode "
                      "(PLIP must use the AP IP fallback)");
    }

    esp_err_t ota_err = ota_server_init();
    if (ota_err != ESP_OK) {
        ESP_LOGE(TAG, "ota_server_init failed: %s", esp_err_to_name(ota_err));
    } else {
        ESP_LOGI(TAG, "OTA server listening on :%d", OTA_SERVER_PORT);

        // Slice 10: piggyback the PLIP /voice/hook endpoint on the same
        // esp_http_server instance. Independent of voice_pipeline_init
        // success — the handler tolerates a degraded pipeline (the
        // voice_pipeline_* APIs return ESP_ERR_INVALID_STATE which we
        // log and report as a 200 with whatever state we have).
        httpd_handle_t httpd = ota_server_get_handle();
        esp_err_t hook_err = voice_hook_endpoint_init(httpd);
        if (hook_err != ESP_OK) {
            ESP_LOGW(TAG, "voice_hook_endpoint_init: %s",
                     esp_err_to_name(hook_err));
        }

        // Slice 12: REST surface for runtime game tuning. Today this
        // exposes /game/group_profile (GET + POST) so the dashboard /
        // GM can swap the hints policy without reflashing NVS. The
        // POST handler validates via hints_client_set_group_profile()
        // and persists to NVS namespace "zacus" / key "group_profile"
        // (the same slot main.c reads at boot).
        esp_err_t game_err = game_endpoint_init(httpd);
        if (game_err != ESP_OK) {
            ESP_LOGW(TAG, "game_endpoint_init: %s",
                     esp_err_to_name(game_err));
        }
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
            } else {
                // Slice 11 (P5): load the group profile from NVS so the
                // hints engine can tune answers per audience (TECH /
                // NON_TECH / MIXED / BOTH). Default to MIXED when the
                // key is absent or holds an unknown value.
                // TODO(slice-11): endpoint /game/group_profile to update
                // NVS at runtime — for now the value is flashed by the
                // dashboard or `idf.py nvs-partition-gen` outputs.
                nvs_handle_t gh;
                esp_err_t open_err = nvs_open("zacus", NVS_READONLY, &gh);
                char profile[HINTS_CLIENT_GROUP_PROFILE_MAX] = "MIXED";
                if (open_err == ESP_OK) {
                    size_t plen = sizeof(profile);
                    esp_err_t kerr = nvs_get_str(gh, "group_profile",
                                                 profile, &plen);
                    if (kerr != ESP_OK) {
                        ESP_LOGI(TAG, "NVS zacus/group_profile missing (%s) "
                                      "— defaulting to MIXED",
                                 esp_err_to_name(kerr));
                        strncpy(profile, "MIXED", sizeof(profile) - 1);
                        profile[sizeof(profile) - 1] = '\0';
                    }
                    nvs_close(gh);
                } else {
                    ESP_LOGI(TAG, "NVS namespace 'zacus' not found (%s) "
                                  "— defaulting group_profile=MIXED",
                             esp_err_to_name(open_err));
                }
                esp_err_t set_err = hints_client_set_group_profile(profile);
                if (set_err != ESP_OK) {
                    // Validation rejected the NVS value — force MIXED
                    // so the engine still has a usable hint policy.
                    ESP_LOGW(TAG, "group_profile \"%s\" rejected — falling "
                                  "back to MIXED", profile);
                    (void) hints_client_set_group_profile("MIXED");
                }
            }

            // Slice 8: voice → npc_engine routing layer. Must come
            // after npc_engine_init / hints_client_init so the hint
            // fast-path lands on the real backend (fallback: local stub).
            esp_err_t disp_err = voice_dispatcher_init();
            if (disp_err != ESP_OK) {
                ESP_LOGW(TAG, "voice_dispatcher_init failed: %s",
                         esp_err_to_name(disp_err));
            }

            voice_pipeline_config_t voice_cfg;
            voice_pipeline_default_config(&voice_cfg);
            // Slice 6: bring up esp-sr AFE + WakeNet (placeholder
            // wn9_hiesp). Auto-start capture so the wake detector is
            // hot from boot — saying "Hi ESP" fires the callback below.
            voice_cfg.enable_wake_word   = true;
            voice_cfg.auto_start_capture = true;
            // Slice 7: stream post-AFE PCM to the MacStudio voice-bridge
            // over WebSocket once the wake word fires. The bridge runs
            // STT (whisper) and may auto-route to the LLM intent layer.
            voice_cfg.voice_bridge_ws_url = ZACUS_VOICE_BRIDGE_WS_URL;
            // Slice 9 + 10: enable the I2S TX leg so TTS payloads coming
            // back from the bridge land on the MAX98357A DAC. Wake-word
            // stays enabled — PLIP hook is the primary path for voice
            // sessions, but "hi esp" remains a backup if the phone is
            // unplugged or its hook switch fails.
            voice_cfg.enable_tts_playback = true;
            voice_pipeline_set_wake_callback(on_voice_wake, NULL);
            voice_pipeline_set_stt_callback(on_voice_stt, NULL);
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
