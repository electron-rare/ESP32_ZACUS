// main.c — P1 Séquence Sonore puzzle firmware
// Hardware: ESP32-S3-DevKitC-1-N16R8 + MAX98357A I2S amp + 8Ω speaker
//           + 4 arcade buttons (RED/BLUE/YELLOW/GREEN) + WS2812B 4-LED strip
//
// Game logic: master sends a melody sequence; player must reproduce it
// in the correct order. On success, sends a 2-digit code fragment to BOX-3.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "led_strip.h"
#include "espnow_slave.h"
#include <string.h>
#include <math.h>

static const char *TAG = "P1_SON";

// ---------------------------------------------------------------------------
// GPIO mapping (matches hardware/kicad/p1_son/p1_son.kicad_sch)
// ---------------------------------------------------------------------------
#define GPIO_BTN_RED    15
#define GPIO_BTN_BLUE   16
#define GPIO_BTN_YELLOW 17
#define GPIO_BTN_GREEN  18
#define GPIO_LED_DATA    8
#define I2S_PORT        I2S_NUM_0
#define I2S_BCLK         4
#define I2S_LRCLK        5
#define I2S_DIN          6
#define LED_COUNT        4

// Button frequencies: C4=262Hz E4=330Hz G4=392Hz C5=523Hz
static const uint32_t kFreqs[4]     = {262, 330, 392, 523};
static const uint8_t  kBtnGpios[4]  = {GPIO_BTN_RED, GPIO_BTN_BLUE,
                                        GPIO_BTN_YELLOW, GPIO_BTN_GREEN};

// LED colours per button index
static const uint8_t kColors[4][3] = {
    {255,  0,  0},   // RED
    {  0,  0,255},   // BLUE
    {255,255,  0},   // YELLOW
    {  0,255,  0},   // GREEN
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint8_t          s_target_seq[8];
static uint8_t          s_seq_len   = 4;    // overridden by MSG_PUZZLE_CONFIG
static uint8_t          s_player_seq[8];
static uint8_t          s_player_pos = 0;
static bool             s_solved    = false;
static uint8_t          s_attempts  = 0;
static uint32_t         s_start_ms  = 0;

static led_strip_handle_t  s_leds;
static i2s_chan_handle_t   s_i2s_tx;

// ---------------------------------------------------------------------------
// I2S helpers
// ---------------------------------------------------------------------------

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT,
                                                            I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws   = I2S_LRCLK,
            .dout = I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false,
                              .ws_inv   = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx));
}

// Generate and play a sine-wave tone at freq Hz for duration_ms.
static void play_tone(uint8_t button_idx, uint32_t duration_ms)
{
    uint32_t freq    = kFreqs[button_idx];
    uint32_t sr      = 44100;
    uint32_t samples = (sr * duration_ms) / 1000;

    // Allocate on heap to avoid stack overflow
    int16_t *buf = malloc(samples * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "OOM for tone buffer");
        return;
    }
    for (uint32_t i = 0; i < samples; i++) {
        float t    = (float)i / (float)sr;
        float env  = (i < 441) ? ((float)i / 441.0f) :    // 10 ms attack
                     (i > samples - 441) ? ((float)(samples - i) / 441.0f) : 1.0f;
        buf[i] = (int16_t)(8192 * env * sinf(2.0f * 3.14159265f * freq * t));
    }

    // Light the LED while playing
    led_strip_set_pixel(s_leds, button_idx,
                        kColors[button_idx][0],
                        kColors[button_idx][1],
                        kColors[button_idx][2]);
    led_strip_refresh(s_leds);

    size_t written;
    i2s_channel_write(s_i2s_tx, buf, samples * sizeof(int16_t),
                      &written, pdMS_TO_TICKS(duration_ms + 100));
    free(buf);

    led_strip_set_pixel(s_leds, button_idx, 0, 0, 0);
    led_strip_refresh(s_leds);
}

// ---------------------------------------------------------------------------
// Sequence logic
// ---------------------------------------------------------------------------

static void play_sequence(void)
{
    ESP_LOGI(TAG, "Playing target sequence (len=%d)", s_seq_len);
    for (uint8_t i = 0; i < s_seq_len; i++) {
        play_tone(s_target_seq[i], 500);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void flash_all_leds(uint8_t r, uint8_t g, uint8_t b, int times)
{
    for (int t = 0; t < times; t++) {
        for (int j = 0; j < LED_COUNT; j++)
            led_strip_set_pixel(s_leds, j, r, g, b);
        led_strip_refresh(s_leds);
        vTaskDelay(pdMS_TO_TICKS(300));
        for (int j = 0; j < LED_COUNT; j++)
            led_strip_set_pixel(s_leds, j, 0, 0, 0);
        led_strip_refresh(s_leds);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void check_sequence(void)
{
    bool correct = (memcmp(s_player_seq, s_target_seq, s_seq_len) == 0);
    if (correct) {
        s_solved = true;
        flash_all_leds(0, 255, 0, 3);

        // Code fragment: XOR-encode the sequence to produce a 2-digit value
        uint8_t val = 0;
        for (int i = 0; i < s_seq_len; i++)
            val ^= (uint8_t)(s_target_seq[i] * (i + 1));
        val = val % 100;

        uint8_t code[4] = { val / 10, val % 10, 0, 0 };
        uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)
                         - s_start_ms;
        espnow_slave_notify_solved(code, elapsed);
        ESP_LOGI(TAG, "Solved! Code=%d%d elapsed=%lu ms",
                 code[0], code[1], elapsed);
    } else {
        s_attempts++;
        flash_all_leds(255, 0, 0, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_player_pos = 0;
        play_sequence();
    }
}

// ---------------------------------------------------------------------------
// ESP-NOW command handler
// ---------------------------------------------------------------------------

static void espnow_cmd_handler(espnow_msg_type_t type,
                                const uint8_t *payload, size_t len)
{
    if (type == MSG_PUZZLE_RESET) {
        s_player_pos = 0;
        s_solved     = false;
        s_attempts   = 0;
        s_start_ms   = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        play_sequence();
        ESP_LOGI(TAG, "Reset");

    } else if (type == MSG_PUZZLE_CONFIG && len >= 1) {
        puzzle_difficulty_t diff = (puzzle_difficulty_t)payload[0];
        if (diff == DIFFICULTY_EASY)   s_seq_len = 3;
        if (diff == DIFFICULTY_NORMAL) s_seq_len = 4;
        if (diff == DIFFICULTY_HARD)   s_seq_len = 5;
        // Re-generate random target sequence
        for (int i = 0; i < s_seq_len; i++)
            s_target_seq[i] = (uint8_t)(esp_random() % 4);
        ESP_LOGI(TAG, "Config: diff=%d seq_len=%d", diff, s_seq_len);
    }
}

// ---------------------------------------------------------------------------
// Button task
// ---------------------------------------------------------------------------

static void button_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_BTN_RED)    | (1ULL << GPIO_BTN_BLUE)
                      | (1ULL << GPIO_BTN_YELLOW)  | (1ULL << GPIO_BTN_GREEN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    uint8_t last_state[4] = {1, 1, 1, 1};

    for (;;) {
        espnow_slave_process();

        if (s_solved) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        for (int i = 0; i < 4; i++) {
            uint8_t cur = gpio_get_level(kBtnGpios[i]);
            if (last_state[i] == 1 && cur == 0) {
                // Button pressed — play tone feedback
                play_tone(i, 300);
                if (s_player_pos < s_seq_len) {
                    s_player_seq[s_player_pos++] = (uint8_t)i;
                    if (s_player_pos == s_seq_len) {
                        check_sequence();
                        s_player_pos = 0;
                    }
                }
            }
            last_state[i] = cur;
        }
        vTaskDelay(pdMS_TO_TICKS(20));   // 50 Hz polling
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi init (required by ESP-NOW)
// ---------------------------------------------------------------------------

static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "P1 Séquence Sonore booting...");

    wifi_init();
    i2s_init();

    // LED strip (WS2812B via RMT)
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = GPIO_LED_DATA,
        .max_leds       = LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model      = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10000000,   // 10 MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_leds));
    led_strip_clear(s_leds);

    // Default sequence (will be overridden by master config)
    s_seq_len = 4;
    for (int i = 0; i < s_seq_len; i++)
        s_target_seq[i] = (uint8_t)(i % 4);

    // ESP-NOW slave (broadcast master MAC = accept from any master)
    static const uint8_t kMasterMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    ESP_ERROR_CHECK(espnow_slave_init(kMasterMac, 1 /* P1 */));
    espnow_slave_register_callback(espnow_cmd_handler);

    s_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // Play intro sequence after 2 s startup delay
    vTaskDelay(pdMS_TO_TICKS(2000));
    play_sequence();

    xTaskCreate(button_task, "buttons", 8192, NULL, 5, NULL);
}
