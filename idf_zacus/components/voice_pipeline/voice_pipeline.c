// voice_pipeline — ESP-IDF implementation. See voice_pipeline.h for the
// scope of slices 5 and 6. The AFE / WakeNet integration is gated on
// `cfg.enable_wake_word`; if init fails (PSRAM exhausted, model
// partition absent, etc.) we log + degrade silently to the slice-5
// I2S-only capture path so the rest of the firmware still boots.

#include "voice_pipeline.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

static const char *TAG = "voice_pipeline";

// Slice 6 placeholder wake word. Standard Espressif WakeNet9 model
// shipped under permissive license — no commercial agreement required.
// Custom "Professeur Zacus" model lands later (see voice spec P4).
#define VOICE_DEFAULT_WAKE_WORD_NAME "wn9_hiesp"

// Partition holding srmodels.bin (added in partitions.csv as a 1 MB
// SPIFFS region). Must match the partition `Name` column.
#define VOICE_SR_MODEL_PARTITION     "model"

#define CAPTURE_TASK_STACK   8192
#define CAPTURE_TASK_PRIO    5
#define CAPTURE_CHUNK_BYTES  1024  // fallback (slice-5 path) — 16-bit @16 kHz = 32 ms slice

static struct {
    bool                     ready;
    voice_pipeline_config_t  cfg;
    i2s_chan_handle_t        rx_chan;
    voice_state_t            state;
    TaskHandle_t             capture_task;
    bool                     capture_run;

    // Wake-word callback (set independently of init, may be NULL).
    voice_wake_callback_t    wake_cb;
    void                    *wake_cb_ctx;

    // ESP-SR handles. NULL if wake-word not enabled or alloc failed —
    // capture_task uses the dumb I2S read path in that case.
    srmodel_list_t          *sr_models;
    const esp_afe_sr_iface_t *afe_iface;
    esp_afe_sr_data_t       *afe_data;
    char                     wake_word_name[32];
    int                      afe_feed_chunk_samples;   // per-channel
    int                      afe_feed_channel_num;     // mic + ref
} s_pipe = {
    .state = VOICE_STATE_IDLE,
};

void voice_pipeline_default_config(voice_pipeline_config_t *out) {
    if (!out) return;
    out->i2s_bclk_pin       = 14;   // GPIO14 -> SCK
    out->i2s_ws_pin         = 15;   // GPIO15 -> WS
    out->i2s_din_pin        = 22;   // GPIO22 -> SD (INMP441)
    out->sample_rate_hz     = 16000;
    out->auto_start_capture = false;
    out->enable_wake_word   = false;
}

bool voice_pipeline_wake_word_active(void) {
    return (s_pipe.afe_iface != NULL && s_pipe.afe_data != NULL);
}

esp_err_t voice_pipeline_set_wake_callback(voice_wake_callback_t cb,
                                           void *user_ctx) {
    s_pipe.wake_cb     = cb;
    s_pipe.wake_cb_ctx = user_ctx;
    return ESP_OK;
}

static esp_err_t i2s_setup(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_pipe.rx_chan);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(s_pipe.cfg.sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = s_pipe.cfg.i2s_bclk_pin,
            .ws   = s_pipe.cfg.i2s_ws_pin,
            .dout = I2S_GPIO_UNUSED,
            .din  = s_pipe.cfg.i2s_din_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(s_pipe.rx_chan, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_pipe.rx_chan);
        s_pipe.rx_chan = NULL;
        return err;
    }
    return ESP_OK;
}

// Bring up esp-sr AFE + WakeNet. Returns ESP_OK on success. On failure
// the caller logs and falls back to the dumb I2S capture path.
static esp_err_t wake_word_setup(void) {
    s_pipe.sr_models = esp_srmodel_init(VOICE_SR_MODEL_PARTITION);
    if (!s_pipe.sr_models || s_pipe.sr_models->num <= 0) {
        ESP_LOGW(TAG, "esp_srmodel_init('%s') returned no models — "
                       "wake word disabled (check srmodels.bin flashed)",
                 VOICE_SR_MODEL_PARTITION);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "esp-sr loaded %d model(s) from partition '%s'",
             s_pipe.sr_models->num, VOICE_SR_MODEL_PARTITION);
    for (int i = 0; i < s_pipe.sr_models->num; i++) {
        ESP_LOGI(TAG, "  model[%d] = %s", i, s_pipe.sr_models->model_name[i]);
    }

    char *wn_name = esp_srmodel_filter(s_pipe.sr_models, ESP_WN_PREFIX, NULL);
    if (!wn_name) {
        ESP_LOGW(TAG, "no WakeNet model found in partition — wake disabled");
        return ESP_ERR_NOT_FOUND;
    }
    strncpy(s_pipe.wake_word_name, wn_name, sizeof(s_pipe.wake_word_name) - 1);
    s_pipe.wake_word_name[sizeof(s_pipe.wake_word_name) - 1] = '\0';
    ESP_LOGI(TAG, "selected wake model = %s (placeholder, slice 6)",
             s_pipe.wake_word_name);

    // Single mic, no reference channel: input format "M" (one micropohne).
    afe_config_t *afe_cfg = afe_config_init("M", s_pipe.sr_models,
                                            AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_cfg) {
        ESP_LOGW(TAG, "afe_config_init returned NULL");
        return ESP_FAIL;
    }
    // Force the wake model we just discovered, in case the default
    // selection logic picks something we don't want.
    afe_cfg->wakenet_init      = true;
    afe_cfg->wakenet_model_name = s_pipe.wake_word_name;
    // 1-mic SR: AEC needs a reference channel we don't have, SE (BSS)
    // needs >= 2 mics. Disable both, keep NS + VAD + AGC.
    afe_cfg->aec_init = false;
    afe_cfg->se_init  = false;
    afe_cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    s_pipe.afe_iface = esp_afe_handle_from_config(afe_cfg);
    if (!s_pipe.afe_iface) {
        ESP_LOGW(TAG, "esp_afe_handle_from_config returned NULL");
        afe_config_free(afe_cfg);
        return ESP_FAIL;
    }
    s_pipe.afe_data = s_pipe.afe_iface->create_from_config(afe_cfg);
    afe_config_free(afe_cfg);
    if (!s_pipe.afe_data) {
        ESP_LOGW(TAG, "AFE create_from_config failed (likely OOM in PSRAM)");
        s_pipe.afe_iface = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_pipe.afe_feed_chunk_samples = s_pipe.afe_iface->get_feed_chunksize(s_pipe.afe_data);
    s_pipe.afe_feed_channel_num   = s_pipe.afe_iface->get_feed_channel_num(s_pipe.afe_data);

    ESP_LOGI(TAG, "AFE up: feed_chunk=%d samples × %d ch, sample_rate=%d Hz",
             s_pipe.afe_feed_chunk_samples,
             s_pipe.afe_feed_channel_num,
             s_pipe.afe_iface->get_samp_rate(s_pipe.afe_data));
    if (s_pipe.afe_iface->print_pipeline) {
        s_pipe.afe_iface->print_pipeline(s_pipe.afe_data);
    }
    return ESP_OK;
}

static void wake_word_teardown(void) {
    if (s_pipe.afe_data && s_pipe.afe_iface && s_pipe.afe_iface->destroy) {
        s_pipe.afe_iface->destroy(s_pipe.afe_data);
    }
    s_pipe.afe_data  = NULL;
    s_pipe.afe_iface = NULL;
    if (s_pipe.sr_models) {
        esp_srmodel_deinit(s_pipe.sr_models);
        s_pipe.sr_models = NULL;
    }
}

// Capture task. Two modes:
//   * AFE active (esp-sr loaded)  : feed I2S into AFE, fetch results,
//                                   detect wake → fire callback.
//   * AFE inactive (slice-5 stub) : log a heartbeat every ~1.6 s.
static void capture_task(void *pv) {
    if (voice_pipeline_wake_word_active()) {
        const int chunk_samples  = s_pipe.afe_feed_chunk_samples;
        const int chunk_channels = s_pipe.afe_feed_channel_num;
        const size_t feed_bytes  = (size_t) chunk_samples * chunk_channels * sizeof(int16_t);

        int16_t *feed_buf = heap_caps_malloc(feed_bytes,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!feed_buf) {
            ESP_LOGE(TAG, "PSRAM alloc for AFE feed buffer failed (%u B) — "
                          "stopping capture", (unsigned) feed_bytes);
            s_pipe.capture_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGI(TAG, "capture_task: AFE mode — chunk=%d samples × %d ch (%u B)",
                 chunk_samples, chunk_channels, (unsigned) feed_bytes);

        size_t bytes_read = 0;
        uint32_t feeds = 0;
        while (s_pipe.capture_run) {
            // INMP441 mono I2S: read directly into the feed buffer (1 ch).
            // If chunk_channels > 1 (e.g. with reference), this would need
            // interleaving — slice-6 single-mic path keeps it simple.
            esp_err_t err = i2s_channel_read(s_pipe.rx_chan, feed_buf,
                                             feed_bytes, &bytes_read,
                                             pdMS_TO_TICKS(200));
            if (err == ESP_ERR_TIMEOUT) {
                continue;
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "i2s read err: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            if (bytes_read == 0) continue;

            s_pipe.afe_iface->feed(s_pipe.afe_data, feed_buf);

            // Drain anything available without blocking the feed cadence.
            afe_fetch_result_t *res = s_pipe.afe_iface->fetch_with_delay(
                s_pipe.afe_data, 0);
            if (res && res->ret_value == ESP_OK) {
                if (res->wakeup_state == WAKENET_DETECTED) {
                    const char *word = s_pipe.wake_word_name;
                    ESP_LOGI(TAG, "WAKE detected: word=%s vol=%.1fdB chan=%d",
                             word, res->data_volume, res->trigger_channel_id);
                    voice_pipeline_set_state(VOICE_STATE_LISTENING);
                    if (s_pipe.wake_cb) {
                        s_pipe.wake_cb(word, s_pipe.wake_cb_ctx);
                    }
                }
            }

            if (++feeds % 100 == 0) {
                ESP_LOGD(TAG, "AFE feed heartbeat: %u chunks", (unsigned) feeds);
            }
        }
        free(feed_buf);
    } else {
        // Slice-5 fallback: dumb capture, no detection.
        static uint8_t buf[CAPTURE_CHUNK_BYTES];
        size_t bytes_read = 0;
        uint32_t total = 0;
        uint32_t ticks = 0;
        ESP_LOGI(TAG, "capture_task: stub mode (no esp-sr)");
        while (s_pipe.capture_run) {
            esp_err_t err = i2s_channel_read(s_pipe.rx_chan, buf, sizeof(buf),
                                             &bytes_read, pdMS_TO_TICKS(100));
            if (err == ESP_OK) {
                total += bytes_read;
                if (++ticks % 50 == 0) {
                    ESP_LOGI(TAG, "capture heartbeat: %u bytes total",
                             (unsigned) total);
                }
            } else if (err != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "i2s read err: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
    }
    s_pipe.capture_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t voice_pipeline_init(const voice_pipeline_config_t *config) {
    if (s_pipe.ready) return ESP_OK;
    voice_pipeline_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        voice_pipeline_default_config(&cfg);
    }
    s_pipe.cfg = cfg;

    esp_err_t err = i2s_setup();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s setup failed: %s — staying idle without capture",
                 esp_err_to_name(err));
        // Don't fail init: we still want the state machine to be usable so
        // the rest of the system (npc_engine, REST surface) can wire calls.
        s_pipe.ready = true;
        s_pipe.state = VOICE_STATE_IDLE;
        return ESP_OK;
    }

    if (cfg.enable_wake_word) {
        esp_err_t we = wake_word_setup();
        if (we != ESP_OK) {
            ESP_LOGW(TAG, "wake_word_setup failed: %s — degrading to stub capture",
                     esp_err_to_name(we));
            // Don't fail init — the rest of the firmware should still
            // come up. The capture task will run in slice-5 stub mode.
            wake_word_teardown();
        }
    }

    s_pipe.ready = true;
    s_pipe.state = VOICE_STATE_IDLE;
    ESP_LOGI(TAG, "ready (BCLK=%d WS=%d DIN=%d @%u Hz, wake=%s)",
             cfg.i2s_bclk_pin, cfg.i2s_ws_pin, cfg.i2s_din_pin,
             (unsigned) cfg.sample_rate_hz,
             voice_pipeline_wake_word_active() ? s_pipe.wake_word_name : "off");

    if (cfg.auto_start_capture) {
        return voice_pipeline_start_capture();
    }
    return ESP_OK;
}

esp_err_t voice_pipeline_start_capture(void) {
    if (!s_pipe.ready) return ESP_ERR_INVALID_STATE;
    if (!s_pipe.rx_chan) return ESP_ERR_INVALID_STATE;
    if (s_pipe.capture_task) return ESP_OK;  // already running
    esp_err_t err = i2s_channel_enable(s_pipe.rx_chan);
    if (err != ESP_OK) return err;
    s_pipe.capture_run = true;
    if (xTaskCreate(capture_task, "voice_capture", CAPTURE_TASK_STACK, NULL,
                    CAPTURE_TASK_PRIO, &s_pipe.capture_task) != pdPASS) {
        s_pipe.capture_run = false;
        i2s_channel_disable(s_pipe.rx_chan);
        return ESP_ERR_NO_MEM;
    }
    voice_pipeline_set_state(VOICE_STATE_LISTENING);
    return ESP_OK;
}

esp_err_t voice_pipeline_stop_capture(void) {
    if (!s_pipe.ready) return ESP_ERR_INVALID_STATE;
    if (!s_pipe.capture_task) return ESP_OK;
    s_pipe.capture_run = false;
    // Task observes the flag and self-deletes on its next tick.
    if (s_pipe.rx_chan) {
        i2s_channel_disable(s_pipe.rx_chan);
    }
    voice_pipeline_set_state(VOICE_STATE_IDLE);
    return ESP_OK;
}

voice_state_t voice_pipeline_get_state(void) {
    return s_pipe.state;
}

esp_err_t voice_pipeline_set_state(voice_state_t state) {
    s_pipe.state = state;
    return ESP_OK;
}
