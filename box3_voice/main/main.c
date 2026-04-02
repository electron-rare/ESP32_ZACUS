/*
 * Zacus BOX-3 Voice — ESP-IDF Application Entry Point
 *
 * ESP32-S3-BOX-3 voice pipeline for "Le Mystere du Professeur Zacus"
 * escape room game. Handles wake-word detection, voice capture,
 * WebSocket streaming to the mascarade voice bridge, and speaker output.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "voice_ws_client.h"

/* BSP header — provided by espressif/esp-box component */
#include "bsp/esp-bsp.h"

static const char *TAG = "zacus-voice";

/* --------------- Shared state --------------- */

static i2s_chan_handle_t s_spk_handle = NULL;   /* Speaker TX channel */
static i2s_chan_handle_t s_mic_handle = NULL;   /* Microphone RX channel */
static volatile bool     s_wifi_connected = false;
static volatile bool     s_voice_streaming = false;

/* --------------- Forward declarations --------------- */

static esp_err_t wifi_init_sta(void);
static void      audio_test_tone(void);
static void      mic_monitor_task(void *arg);
static void      tts_audio_callback(const uint8_t *data, size_t len);

/* --------------- WiFi event handler --------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected — IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
    }
}

/* --------------- WiFi station init --------------- */

static esp_err_t wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = CONFIG_ZACUS_WIFI_SSID,
            .password = CONFIG_ZACUS_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    /* Allow open networks if no password configured */
    if (strlen(CONFIG_ZACUS_WIFI_PASSWORD) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started, connecting to '%s'...", CONFIG_ZACUS_WIFI_SSID);
    return ESP_OK;
}

/* --------------- Test tone (440 Hz sine) --------------- */

static void audio_test_tone(void)
{
    if (!s_spk_handle) {
        ESP_LOGW(TAG, "Speaker not initialized, skipping test tone");
        return;
    }

    ESP_LOGI(TAG, "Playing 440 Hz test tone (1 second)...");

    /* Generate 1 second of 440 Hz sine wave using persistent speaker handle */
    const int duration_ms = 1000;
    const int total_samples = AUDIO_SAMPLE_RATE * duration_ms / 1000;
    const float frequency = 440.0f;
    const float amplitude = 16000.0f;

    int16_t buffer[256];
    size_t bytes_written = 0;
    int sample_idx = 0;

    while (sample_idx < total_samples) {
        int chunk = (total_samples - sample_idx < 256) ? (total_samples - sample_idx) : 256;
        for (int i = 0; i < chunk; i++) {
            float t = (float)(sample_idx + i) / (float)AUDIO_SAMPLE_RATE;
            buffer[i] = (int16_t)(amplitude * sinf(2.0f * M_PI * frequency * t));
        }
        i2s_channel_write(s_spk_handle, buffer, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        sample_idx += chunk;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); /* Let buffer drain */
    ESP_LOGI(TAG, "Test tone complete");
}

/* --------------- TTS audio callback --------------- */

static void tts_audio_callback(const uint8_t *data, size_t len)
{
    /*
     * Called from WebSocket event context with incoming TTS PCM16 data.
     * Write directly to the speaker I2S channel.
     */
    if (!s_spk_handle || len == 0) {
        return;
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(s_spk_handle, data, len,
                                       &bytes_written, pdMS_TO_TICKS(200));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TTS write to speaker failed: %s", esp_err_to_name(ret));
    }
}

/* --------------- Speaker init --------------- */

static esp_err_t speaker_init(void)
{
    /* Enable power amplifier */
    gpio_set_direction(BOX3_PA_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(BOX3_PA_ENABLE, 1);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_spk_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BOX3_I2S_MCLK,
            .bclk = BOX3_I2S_BCLK,
            .ws   = BOX3_I2S_WS,
            .dout = BOX3_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_spk_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_spk_handle));

    ESP_LOGI(TAG, "Speaker I2S initialized (I2S_NUM_0)");
    return ESP_OK;
}

/* --------------- Microphone task --------------- */

static void mic_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Starting mic capture task...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_mic_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOX3_I2S_BCLK,
            .ws   = BOX3_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = BOX3_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_mic_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_mic_handle));

    int16_t buffer[AUDIO_FRAME_SAMPLES];
    size_t bytes_read = 0;
    int rms_log_counter = 0;

    while (1) {
        esp_err_t ret = i2s_channel_read(s_mic_handle, buffer,
                                          AUDIO_FRAME_SAMPLES * sizeof(int16_t),
                                          &bytes_read, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Mic read error: %s", esp_err_to_name(ret));
            continue;
        }

        int samples = bytes_read / sizeof(int16_t);

        /* If voice streaming is active, send PCM to bridge */
        if (s_voice_streaming) {
            voice_state_t st = voice_ws_get_state();
            if (st == VOICE_STATE_LISTENING || st == VOICE_STATE_READY) {
                voice_ws_send_audio(buffer, samples);
            }
        }

        /* Log RMS periodically (every ~500ms = 25 frames at 20ms) */
        rms_log_counter++;
        if (rms_log_counter >= 25) {
            rms_log_counter = 0;
            double sum_sq = 0.0;
            for (int i = 0; i < samples; i++) {
                sum_sq += (double)buffer[i] * (double)buffer[i];
            }
            double rms = sqrt(sum_sq / samples);
            float db = 20.0f * log10f((float)rms + 1.0f);
            ESP_LOGI(TAG, "Mic RMS: %.1f  (~%.1f dB)  streaming=%s",
                     rms, db, s_voice_streaming ? "yes" : "no");
        }
    }
}

/* --------------- Voice bridge connection task --------------- */

static void voice_bridge_task(void *arg)
{
    /* Wait for WiFi to be connected */
    while (!s_wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "WiFi connected — initializing voice bridge...");

    /* Small delay for network stack to settle */
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ret = voice_ws_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Voice WS init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    /* Set TTS audio callback to play through speaker */
    voice_ws_set_audio_callback(tts_audio_callback);

    ret = voice_ws_connect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Voice bridge connected and ready!");
        /* Enable streaming — for now always on (wake-word stub).
         * TODO: Gate this behind actual wake-word detection from ESP-SR.
         * For prototype: use BOX-3 BOOT button (GPIO0) to toggle streaming. */
        s_voice_streaming = true;
        ESP_LOGI(TAG, "Voice streaming enabled (continuous mode)");
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Bridge handshake timed out — will keep retrying via reconnect");
        /* Streaming will be enabled when hello_ack arrives */
        s_voice_streaming = true;
    } else {
        ESP_LOGE(TAG, "Voice bridge connection failed: %s", esp_err_to_name(ret));
    }

    vTaskDelete(NULL);
}

/* --------------- Button handler (BOOT button = GPIO0) --------------- */

static void button_task(void *arg)
{
    /* Use BOOT button (GPIO0) to toggle voice streaming on/off.
     * This is a wake-word placeholder — press to start/stop listening.
     */
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

    bool last_state = true; /* Pull-up: idle = high */

    while (1) {
        bool current = gpio_get_level(GPIO_NUM_0);

        /* Detect falling edge (button press) */
        if (last_state && !current) {
            s_voice_streaming = !s_voice_streaming;
            ESP_LOGI(TAG, "BOOT button pressed — streaming %s",
                     s_voice_streaming ? "ON" : "OFF");

            if (!s_voice_streaming) {
                voice_ws_send_listen_stop();
            }

            /* Simple debounce */
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        last_state = current;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* --------------- Application entry point --------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  Zacus BOX-3 Voice Pipeline");
    ESP_LOGI(TAG, "  Board: ESP32-S3-BOX-3");
    ESP_LOGI(TAG, "  IDF:   %s", esp_get_idf_version());
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "============================================");

    /* Initialize NVS (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize BSP (display, codec, buttons) */
    ESP_LOGI(TAG, "Initializing BSP...");
    bsp_display_start();
    ESP_LOGI(TAG, "Display initialized (%dx%d)", BOX3_LCD_WIDTH, BOX3_LCD_HEIGHT);

    /* Initialize persistent speaker output (used for test tone + TTS playback) */
    speaker_init();

    /* Play test tone to verify audio output */
    audio_test_tone();

    /* Start mic capture task (reads I2S mic, streams to bridge when active) */
    xTaskCreatePinnedToCore(mic_monitor_task, "mic_capture", 4096, NULL, 5, NULL, 1);

    /* Start button handler (BOOT button toggles voice streaming) */
    xTaskCreate(button_task, "button", 2048, NULL, 4, NULL);

    /* Connect to WiFi */
    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta();

    /* Start voice bridge connection task (waits for WiFi, then connects WS) */
    xTaskCreate(voice_bridge_task, "voice_bridge", 6144, NULL, 5, NULL);

    /* TODO: Initialize ESP-SR WakeNet for wake-word detection
     *   - Load WakeNet9 model ("hi esp" or custom)
     *   - Feed audio frames from mic to WakeNet
     *   - On wake-word detection, set s_voice_streaming = true
     *   - Replace BOOT button toggle with wake-word trigger
     */

    /* TODO: Game logic integration
     *   - Subscribe to game state events (puzzle solved, time updates)
     *   - Display status on ILI9341 (puzzle progress, timer)
     *   - LED ring feedback for voice activity
     */

    /* TODO: OTA update support
     *   - Check for firmware updates on boot
     *   - Use ota_0 partition for rollback safety
     */

    ESP_LOGI(TAG, "Initialization complete. Press BOOT button to toggle voice streaming.");
}
