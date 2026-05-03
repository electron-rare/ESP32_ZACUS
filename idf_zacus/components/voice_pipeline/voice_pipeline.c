#include "voice_pipeline.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "voice_pipeline";

#define CAPTURE_TASK_STACK   4096
#define CAPTURE_TASK_PRIO    4
#define CAPTURE_CHUNK_BYTES  1024  // 16-bit @16 kHz = 32 ms slice

static struct {
    bool                     ready;
    voice_pipeline_config_t  cfg;
    i2s_chan_handle_t        rx_chan;
    voice_state_t            state;
    TaskHandle_t             capture_task;
    bool                     capture_run;
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

static void capture_task(void *pv) {
    static uint8_t buf[CAPTURE_CHUNK_BYTES];
    size_t bytes_read = 0;
    uint32_t total = 0;
    uint32_t ticks = 0;
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

    s_pipe.ready = true;
    s_pipe.state = VOICE_STATE_IDLE;
    ESP_LOGI(TAG, "ready (BCLK=%d WS=%d DIN=%d @%u Hz)",
             cfg.i2s_bclk_pin, cfg.i2s_ws_pin, cfg.i2s_din_pin,
             (unsigned) cfg.sample_rate_hz);

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
