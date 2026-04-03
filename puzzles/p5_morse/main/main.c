// main.c — P5 Code Morse puzzle firmware
// Hardware: ESP32-DevKit-C + brass telegraph key + active buzzer
//           + red/green/white LEDs
//
// Game logic: the puzzle transmits "ZACUS" in Morse; the player must
// tap back the same sequence on the telegraph key.
// On success, sends digit 5 as code fragment to BOX-3.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "espnow_slave.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "P5_MORSE";

// ---------------------------------------------------------------------------
// GPIO mapping (matches hardware/kicad/p5_morse/p5_morse.kicad_sch)
// ---------------------------------------------------------------------------
#define GPIO_KEY    4   // telegraph key — LOW when pressed
#define GPIO_BUZZER 5
#define GPIO_LED_R  6   // red  LED: key-pressed indicator
#define GPIO_LED_G  7   // green LED: message valid / solved
#define GPIO_LED_W  8   // white LED: Morse light-mode for NON_TECH

// ---------------------------------------------------------------------------
// Morse timing (ms)
// ---------------------------------------------------------------------------
#define DOT_MIN_MS    50
#define DOT_MAX_MS    300
#define DASH_MIN_MS   301
#define DASH_MAX_MS   1500
#define LETTER_GAP_MS 700    // silence between letters triggers decode
#define WORD_GAP_MS  1600    // silence after word triggers word check

// Target message: ZACUS
// Z=--.. A=.- C=-.-. U=..- S=...
static const char *kTargetWord = "ZACUS";

// Morse table A-Z
static const char *kMorseTable[26] = {
    ".-",    "-...",  "-.-.",  "-..",   ".",     "..-.",  "--.",
    "....",  "..",    ".---",  "-.-",   ".-..",  "--",    "-.",
    "---",   ".--.",  "--.-",  ".-.",   "...",   "-",     "..-",
    "...-",  ".--",   "-..-",  "-.--",  "--.."
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static char    s_received[16]        = {0};
static uint8_t s_recv_pos            = 0;
static char    s_current_symbol[10]  = {0};
static uint8_t s_sym_pos             = 0;
static bool    s_solved              = false;
static bool    s_light_mode          = false;  // NON_TECH variant
static uint32_t s_start_ms           = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void buzzer_on(void)  { gpio_set_level(GPIO_BUZZER, 1); }
static void buzzer_off(void) { gpio_set_level(GPIO_BUZZER, 0); }

// ---------------------------------------------------------------------------
// Transmit target sequence to player (audio buzzer or visual LED)
// ---------------------------------------------------------------------------

static void transmit_target(void)
{
    ESP_LOGI(TAG, "Transmitting target: %s", kTargetWord);
    for (int c = 0; kTargetWord[c] != '\0'; c++) {
        int idx = kTargetWord[c] - 'A';
        const char *seq = kMorseTable[idx];
        for (int i = 0; seq[i] != '\0'; i++) {
            uint32_t dur = (seq[i] == '.') ? 150 : 450;
            if (s_light_mode) {
                gpio_set_level(GPIO_LED_W, 1);
            } else {
                buzzer_on();
            }
            vTaskDelay(pdMS_TO_TICKS(dur));
            if (s_light_mode) {
                gpio_set_level(GPIO_LED_W, 0);
            } else {
                buzzer_off();
            }
            vTaskDelay(pdMS_TO_TICKS(150));   // intra-letter gap
        }
        vTaskDelay(pdMS_TO_TICKS(LETTER_GAP_MS));
    }
}

// ---------------------------------------------------------------------------
// Morse decoder
// ---------------------------------------------------------------------------

static void decode_symbol(void)
{
    if (s_sym_pos == 0) return;
    s_current_symbol[s_sym_pos] = '\0';

    for (int i = 0; i < 26; i++) {
        if (strcmp(kMorseTable[i], s_current_symbol) == 0) {
            char letter = (char)('A' + i);
            if (s_recv_pos < (uint8_t)(sizeof(s_received) - 1)) {
                s_received[s_recv_pos++] = letter;
                s_received[s_recv_pos]   = '\0';
            }
            ESP_LOGI(TAG, "Decoded: %s → %c (so far: %s)",
                     s_current_symbol, letter, s_received);
            break;
        }
    }
    s_sym_pos = 0;
    memset(s_current_symbol, 0, sizeof(s_current_symbol));
}

static void check_word(void)
{
    if (strcmp(s_received, kTargetWord) == 0) {
        s_solved = true;
        gpio_set_level(GPIO_LED_G, 1);
        gpio_set_level(GPIO_LED_R, 0);

        // Digit 5 contribution: last character 'S' → positional index = 18 (0-indexed)
        // We use the constant value 5 as the digit (as per scenario spec)
        uint8_t code[4] = {5, 0, 0, 0};
        uint32_t elapsed = now_ms() - s_start_ms;
        espnow_slave_notify_solved(code, elapsed);
        ESP_LOGI(TAG, "SOLVED: ZACUS decoded! elapsed=%lu ms", elapsed);
    } else {
        // Wrong word — flash red, reset
        ESP_LOGW(TAG, "Wrong word received: '%s', expected: '%s'",
                 s_received, kTargetWord);
        gpio_set_level(GPIO_LED_R, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(GPIO_LED_R, 0);
        s_recv_pos = 0;
        memset(s_received, 0, sizeof(s_received));
    }
}

// ---------------------------------------------------------------------------
// ESP-NOW command handler
// ---------------------------------------------------------------------------

static void espnow_cmd_handler(espnow_msg_type_t type,
                                const uint8_t *payload, size_t len)
{
    if (type == MSG_PUZZLE_RESET) {
        s_solved   = false;
        s_recv_pos = 0;
        s_sym_pos  = 0;
        memset(s_received,       0, sizeof(s_received));
        memset(s_current_symbol, 0, sizeof(s_current_symbol));
        gpio_set_level(GPIO_LED_G, 0);
        gpio_set_level(GPIO_LED_R, 0);
        s_start_ms = now_ms();
        transmit_target();
        ESP_LOGI(TAG, "Reset");

    } else if (type == MSG_PUZZLE_CONFIG && len >= 1) {
        // DIFFICULTY_EASY (NON_TECH) → light mode (visual pulses, no sound)
        s_light_mode = (payload[0] == (uint8_t)DIFFICULTY_EASY);
        ESP_LOGI(TAG, "Config: light_mode=%d", s_light_mode);
    }
}

// ---------------------------------------------------------------------------
// Morse input task
// ---------------------------------------------------------------------------

static void morse_task(void *arg)
{
    uint32_t press_start  = 0;
    uint32_t last_release = 0;
    bool     key_down     = false;

    for (;;) {
        espnow_slave_process();

        if (s_solved) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint32_t t     = now_ms();
        bool     pressed = (gpio_get_level(GPIO_KEY) == 0);

        if (pressed && !key_down) {
            // Key press start
            key_down    = true;
            press_start = t;
            buzzer_on();
            gpio_set_level(GPIO_LED_R, 1);

        } else if (!pressed && key_down) {
            // Key release
            key_down          = false;
            uint32_t duration = t - press_start;
            buzzer_off();
            gpio_set_level(GPIO_LED_R, 0);
            last_release = t;

            if (duration >= DOT_MIN_MS && duration <= DOT_MAX_MS) {
                if (s_sym_pos < (uint8_t)(sizeof(s_current_symbol) - 1))
                    s_current_symbol[s_sym_pos++] = '.';
            } else if (duration >= DASH_MIN_MS && duration <= DASH_MAX_MS) {
                if (s_sym_pos < (uint8_t)(sizeof(s_current_symbol) - 1))
                    s_current_symbol[s_sym_pos++] = '-';
            }
            // Ignore if outside valid range (noise / held too long)

        } else if (!key_down && last_release > 0) {
            uint32_t silence = t - last_release;

            // Letter gap: decode accumulated symbol
            if (silence > LETTER_GAP_MS && s_sym_pos > 0) {
                decode_symbol();
                last_release = t;
            }
            // Word gap: check assembled word
            if (silence > WORD_GAP_MS && s_recv_pos > 0) {
                check_word();
                last_release = 0;   // reset to avoid re-triggering
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));   // 100 Hz loop
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi init
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
    ESP_LOGI(TAG, "P5 Code Morse booting...");

    wifi_init();

    // Output GPIOs
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUZZER) | (1ULL << GPIO_LED_R)
                      | (1ULL << GPIO_LED_G)  | (1ULL << GPIO_LED_W),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);

    // Input GPIO: telegraph key with internal pull-up
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << GPIO_KEY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    // ESP-NOW slave
    static const uint8_t kMasterMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    ESP_ERROR_CHECK(espnow_slave_init(kMasterMac, 5 /* P5 */));
    espnow_slave_register_callback(espnow_cmd_handler);

    s_start_ms = now_ms();

    // Wait for master to be ready, then transmit first sequence
    vTaskDelay(pdMS_TO_TICKS(3000));
    transmit_target();

    xTaskCreate(morse_task, "morse", 4096, NULL, 5, NULL);
}
