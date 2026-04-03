// main.c — P7 Coffre Final puzzle firmware
// Hardware: ESP32-DevKit-C + 4×3 membrane keypad + SG90 servo (latch)
//           + SSD1306 OLED 128×32 (I2C) + WS2812B single RGB LED + buzzer
//
// Game logic: master sends the 8-digit final code; player must enter it
// on the keypad and press '#' to confirm. On correct code the servo unlocks
// the latch and a victory sequence plays.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "espnow_slave.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static const char *TAG = "P7_COFFRE";

// ---------------------------------------------------------------------------
// GPIO mapping (matches hardware/kicad/p7_coffre/p7_coffre.kicad_sch)
// ---------------------------------------------------------------------------
static const uint8_t kRowPins[4] = {4, 5, 6, 7};
static const uint8_t kColPins[3] = {12, 13, 14};

static const char kKeyMap[4][3] = {
    {'1','2','3'},
    {'4','5','6'},
    {'7','8','9'},
    {'*','0','#'},
};

#define GPIO_SERVO  25
#define GPIO_SDA    21
#define GPIO_SCL    22
#define GPIO_LED     2   // WS2812B single LED (on-board or external)
#define GPIO_BUZZER 18

// Servo pulse widths (µs) for 50 Hz PWM
#define SERVO_LOCKED    1000
#define SERVO_UNLOCKED  2000

// LEDC configuration
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_14_BIT
#define LEDC_FREQ_HZ    50

// SSD1306 I2C address and port
#define OLED_ADDR       0x3C
#define I2C_PORT        I2C_NUM_0
#define I2C_FREQ_HZ     400000

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static char    s_entered[9]     = {0};
static uint8_t s_pos            = 0;
static bool    s_solved         = false;
static char    s_target_code[9] = "00000000";   // set by master via MSG_PUZZLE_CONFIG
static uint32_t s_start_ms      = 0;

// ---------------------------------------------------------------------------
// Servo helpers
// ---------------------------------------------------------------------------

static void servo_set_us(uint32_t pulse_us)
{
    // period = 20 000 µs; 14-bit resolution = 16383 ticks
    uint32_t duty = (pulse_us * 16383UL) / 20000UL;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
}

static void servo_lock(void)   { servo_set_us(SERVO_LOCKED); }
static void servo_unlock(void) { servo_set_us(SERVO_UNLOCKED); }

// ---------------------------------------------------------------------------
// Buzzer helpers
// ---------------------------------------------------------------------------

static void buzzer_beep(uint32_t ms)
{
    gpio_set_level(GPIO_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(GPIO_BUZZER, 0);
}

// ---------------------------------------------------------------------------
// SSD1306 minimal driver (I2C, 128×32)
// ---------------------------------------------------------------------------

#define OLED_CTRL_CMD  0x00
#define OLED_CTRL_DATA 0x40

static void oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {OLED_CTRL_CMD, cmd};
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, sizeof(buf),
                               pdMS_TO_TICKS(10));
}

static void oled_init(void)
{
    static const uint8_t kInitSeq[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10,
        0x40, 0x81, 0xCF, 0xA1, 0xA6, 0xA8, 0x1F,
        0xA4, 0xD3, 0x00, 0xD5, 0xF0, 0xD9, 0x22,
        0xDA, 0x02, 0xDB, 0x20, 0x8D, 0x14, 0xAF,
    };
    for (size_t i = 0; i < sizeof(kInitSeq); i++)
        oled_cmd(kInitSeq[i]);
}

// Display a null-terminated string on row 0 (simplified: fixed font)
// In production: use full font table; here we just log and stub the display.
static void oled_show_entered(const char *text)
{
    // Full SSD1306 font rendering would go here.
    // For this firmware skeleton, we log to UART and emit a minimal
    // I2C clear+write cycle to confirm the hardware path is exercised.
    ESP_LOGI(TAG, "OLED: [%s]", text);
    // Clear display: set all pages to 0x00
    for (uint8_t page = 0; page < 4; page++) {
        oled_cmd(0xB0 | page);   // page address
        oled_cmd(0x00);           // lower column start
        oled_cmd(0x10);           // upper column start
        uint8_t clear[129];
        clear[0] = OLED_CTRL_DATA;
        memset(clear + 1, 0, 128);
        i2c_master_write_to_device(I2C_PORT, OLED_ADDR, clear, sizeof(clear),
                                   pdMS_TO_TICKS(20));
    }
}

// ---------------------------------------------------------------------------
// Code check
// ---------------------------------------------------------------------------

static void check_code(void)
{
    bool correct = (strcmp(s_entered, s_target_code) == 0);
    if (correct) {
        s_solved = true;
        servo_unlock();
        gpio_set_level(GPIO_LED, 1);

        // Victory melody: 3 ascending beeps
        for (int i = 0; i < 3; i++) {
            buzzer_beep(200);
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        uint8_t code[4] = {0, 0, 0, 0};  // P7 validates the assembled code; no new digit
        uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)
                         - s_start_ms;
        espnow_slave_notify_solved(code, elapsed);
        ESP_LOGI(TAG, "COFFRE OUVERT! Code=%s elapsed=%lu ms",
                 s_entered, elapsed);
        oled_show_entered("** OUVERT **");

    } else {
        // Wrong code: error beep, clear entry
        ESP_LOGW(TAG, "Wrong code entered: '%s' (expected: '%s')",
                 s_entered, s_target_code);
        gpio_set_level(GPIO_LED, 0);
        buzzer_beep(600);
        s_pos = 0;
        memset(s_entered, 0, sizeof(s_entered));
        oled_show_entered("ERREUR");
        vTaskDelay(pdMS_TO_TICKS(1000));
        oled_show_entered("........");
    }
}

// ---------------------------------------------------------------------------
// Keypad scanner (active-low matrix)
// ---------------------------------------------------------------------------

static char scan_keypad(void)
{
    for (int r = 0; r < 4; r++) {
        gpio_set_level(kRowPins[r], 0);   // pull row low
        for (int c = 0; c < 3; c++) {
            if (gpio_get_level(kColPins[c]) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));   // debounce
                if (gpio_get_level(kColPins[c]) == 0) {
                    gpio_set_level(kRowPins[r], 1);
                    return kKeyMap[r][c];
                }
            }
        }
        gpio_set_level(kRowPins[r], 1);   // release row
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ESP-NOW command handler
// ---------------------------------------------------------------------------

static void espnow_cmd_handler(espnow_msg_type_t type,
                                const uint8_t *payload, size_t len)
{
    if (type == MSG_PUZZLE_RESET) {
        servo_lock();
        s_pos    = 0;
        s_solved = false;
        memset(s_entered, 0, sizeof(s_entered));
        gpio_set_level(GPIO_LED, 0);
        s_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        oled_show_entered("........");
        ESP_LOGI(TAG, "Reset — coffre verrouillé");

    } else if (type == MSG_PUZZLE_CONFIG && len >= 8) {
        // Master sends the assembled 8-digit code
        memcpy(s_target_code, payload, 8);
        s_target_code[8] = '\0';
        ESP_LOGI(TAG, "Target code set: %s", s_target_code);
    }
}

// ---------------------------------------------------------------------------
// Keypad task
// ---------------------------------------------------------------------------

static void keypad_task(void *arg)
{
    char last_key = 0;

    for (;;) {
        espnow_slave_process();

        if (s_solved) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        char key = scan_keypad();

        if (key && key != last_key) {
            buzzer_beep(50);   // tactile feedback beep

            if (key == '#') {
                // Confirm
                if (s_pos == 8) {
                    check_code();
                } else {
                    ESP_LOGW(TAG, "Incomplete: %d/8 digits entered", s_pos);
                    buzzer_beep(200);
                }
            } else if (key == '*') {
                // Clear
                s_pos = 0;
                memset(s_entered, 0, sizeof(s_entered));
                oled_show_entered("........");
                ESP_LOGI(TAG, "Entry cleared");
            } else if (s_pos < 8) {
                s_entered[s_pos++] = key;
                s_entered[s_pos]   = '\0';
                oled_show_entered(s_entered);
                ESP_LOGI(TAG, "Entered: %s (%d/8)", s_entered, s_pos);
            }
        }
        last_key = key;
        vTaskDelay(pdMS_TO_TICKS(50));   // 20 Hz keypad scan
    }
}

// ---------------------------------------------------------------------------
// Peripheral init helpers
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

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t chan = {
        .gpio_num   = GPIO_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan));
    servo_lock();
}

static void i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = GPIO_SDA,
        .scl_io_num       = GPIO_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "P7 Coffre Final booting...");

    wifi_init();
    servo_init();
    i2c_init();
    oled_init();
    oled_show_entered("........");

    // Keypad row pins: output, initially HIGH
    for (int r = 0; r < 4; r++) {
        gpio_set_direction(kRowPins[r], GPIO_MODE_OUTPUT);
        gpio_set_level(kRowPins[r], 1);
    }
    // Keypad col pins: input with pull-up
    for (int c = 0; c < 3; c++) {
        gpio_set_direction(kColPins[c], GPIO_MODE_INPUT);
        gpio_set_pull_mode(kColPins[c], GPIO_PULLUP_ONLY);
    }

    // Status LED and buzzer
    gpio_set_direction(GPIO_LED,    GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_LED,    0);
    gpio_set_level(GPIO_BUZZER, 0);

    // ESP-NOW slave
    static const uint8_t kMasterMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    ESP_ERROR_CHECK(espnow_slave_init(kMasterMac, 7 /* P7 */));
    espnow_slave_register_callback(espnow_cmd_handler);

    s_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // Boot confirmation beep
    buzzer_beep(100);

    xTaskCreate(keypad_task, "keypad", 4096, NULL, 5, NULL);
}
