// Zacus master — ESP-IDF entry point (P1 first slice).
//
// Responsibilities at this slice:
//   1. Initialize NVS (required by Wi-Fi / esp_event later).
//   2. Mount the LittleFS "storage" partition on /littlefs and list contents.
//   3. Log heap stats (internal + PSRAM) for baseline measurement.
//   4. Idle loop with periodic heartbeat (no deep sleep — keeps the inherited
//      OTA server task alive once we wire it in P1 slice 2).
//
// Subsequent slices port the NPC engine, voice pipeline, media manager, etc.
// See docs/superpowers/specs/2026-05-03-voice-pipeline-esp-sr-design.md.

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"

static const char *TAG = "zacus_main";

// ─── ota_server externs (real implementations come with the puzzle/master
//     state in a later P1 slice; for now we provide trivial stubs so the
//     component links cleanly). ─────────────────────────────────────────────
int puzzle_get_battery_pct(void) {
    return 100;
}

int puzzle_get_espnow_peer_count(void) {
    return 0;
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

// ─── app_main ────────────────────────────────────────────────────────────────

void app_main(void) {
    ESP_LOGI(TAG, "Zacus master booting (ESP-IDF scaffold, P1 first slice)");

    log_heap_stats("boot");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS initialized");

    if (mount_littlefs() == ESP_OK) {
        list_littlefs_root();
    }

    log_heap_stats("post-init");

    ESP_LOGI(TAG, "entering idle loop (heartbeat every 60 s)");
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        tick++;
        ESP_LOGI(TAG, "heartbeat #%u — uptime=%llu s",
                 (unsigned) tick,
                 (unsigned long long) (esp_log_timestamp() / 1000));
    }
}
