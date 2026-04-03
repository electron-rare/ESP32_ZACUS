// main.c — P6 Symboles Alchimiques (NFC-based) puzzle firmware
// Hardware: ESP32-DevKit-C + MFRC522 NFC reader (SPI)
//           + active buzzer + green/red LEDs
//           + 12 NTAG213 NFC tags on wooden symbol pieces
//
// Game logic: player places 12 alchemical symbol pieces on a wooden tablet
// in the correct order; each piece has an embedded NFC tag.
// MFRC522 detects tags sequentially as they are placed.
// On correct full configuration, sends 2-digit code fragment to BOX-3.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "espnow_slave.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "P6_SYMBOLES";

// ---------------------------------------------------------------------------
// GPIO mapping (matches hardware/kicad/p6_symboles/p6_symboles.kicad_sch)
// ---------------------------------------------------------------------------
#define GPIO_BUZZER  25
#define GPIO_LED_G   26
#define GPIO_LED_R   27

// SPI pins for MFRC522
#define SPI_HOST_ID  SPI2_HOST
#define GPIO_CS       5   // SDA/CS
#define GPIO_SCK     18
#define GPIO_MISO    19
#define GPIO_MOSI    23
#define GPIO_RST      4

// ---------------------------------------------------------------------------
// MFRC522 register map (subset needed for basic anticoll polling)
// ---------------------------------------------------------------------------
#define MFRC522_REG_COMMAND      0x01
#define MFRC522_REG_COMIEN       0x02
#define MFRC522_REG_COMIRQ       0x04
#define MFRC522_REG_ERROR        0x06
#define MFRC522_REG_STATUS2      0x08
#define MFRC522_REG_FIFODATA     0x09
#define MFRC522_REG_FIFOLEVEL    0x0A
#define MFRC522_REG_CONTROL      0x0C
#define MFRC522_REG_BITFRAMING   0x0D
#define MFRC522_REG_COLL         0x0E
#define MFRC522_REG_MODE         0x11
#define MFRC522_REG_TXCONTROL    0x14
#define MFRC522_REG_TXASK        0x15
#define MFRC522_CMD_IDLE         0x00
#define MFRC522_CMD_TRANSCEIVE   0x0C
#define MFRC522_CMD_SOFTRESET    0x0F

// ---------------------------------------------------------------------------
// NFC tag → symbol ID mapping
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t uid[4];
    uint8_t symbol_id;   // 1-12
} nfc_tag_t;

// Correct placement order: [7, 2, 11, 4, 9, 1, 8, 3, 12, 6, 10, 5]
static const uint8_t kCorrectOrder[12] = {7, 2, 11, 4, 9, 1, 8, 3, 12, 6, 10, 5};

// Tag UIDs populated from NVS at boot (written during factory setup)
static nfc_tag_t s_tags[12];

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint8_t  s_slots[12]  = {0};   // s_slots[position] = symbol_id placed
static uint8_t  s_placed     = 0;
static bool     s_solved     = false;
static uint32_t s_start_ms   = 0;

// SPI device handle
static spi_device_handle_t s_spi;

// ---------------------------------------------------------------------------
// MFRC522 low-level SPI helpers
// ---------------------------------------------------------------------------

static uint8_t mfrc_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(s_spi, &t);
    return rx[1];
}

static void mfrc_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    spi_device_transmit(s_spi, &t);
}

static void mfrc_set_bit(uint8_t reg, uint8_t mask)
{
    mfrc_write_reg(reg, mfrc_read_reg(reg) | mask);
}

static void mfrc_clear_bit(uint8_t reg, uint8_t mask)
{
    mfrc_write_reg(reg, mfrc_read_reg(reg) & (~mask));
}

// Antenna on
static void mfrc_antenna_on(void)
{
    uint8_t v = mfrc_read_reg(MFRC522_REG_TXCONTROL);
    if ((v & 0x03) != 0x03)
        mfrc_set_bit(MFRC522_REG_TXCONTROL, 0x03);
}

// Soft reset
static void mfrc_reset(void)
{
    mfrc_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));
    mfrc_write_reg(MFRC522_REG_MODE,     0x3D);
    mfrc_write_reg(MFRC522_REG_TXASK,    0x40);
    mfrc_antenna_on();
}

// Transceive helper: send data[send_len], receive into recv, returns recv_len
static int mfrc_transceive(const uint8_t *send, uint8_t send_len,
                            uint8_t *recv,  uint8_t recv_max)
{
    mfrc_write_reg(MFRC522_REG_COMMAND,    MFRC522_CMD_IDLE);
    mfrc_write_reg(MFRC522_REG_COMIRQ,     0x7F);
    mfrc_write_reg(MFRC522_REG_FIFOLEVEL,  0x80);  // flush FIFO
    for (uint8_t i = 0; i < send_len; i++)
        mfrc_write_reg(MFRC522_REG_FIFODATA, send[i]);
    mfrc_write_reg(MFRC522_REG_COMMAND,    MFRC522_CMD_TRANSCEIVE);
    mfrc_set_bit(MFRC522_REG_BITFRAMING,   0x80);  // StartSend

    uint32_t deadline = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)
                      + 36;
    for (;;) {
        uint8_t irq = mfrc_read_reg(MFRC522_REG_COMIRQ);
        if (irq & 0x30) break;  // RxIRq or IdleIRq
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now >= deadline) return -1;
        taskYIELD();
    }
    mfrc_clear_bit(MFRC522_REG_BITFRAMING, 0x80);

    if (mfrc_read_reg(MFRC522_REG_ERROR) & 0x1B) return -1;  // error flags

    int n = mfrc_read_reg(MFRC522_REG_FIFOLEVEL);
    if (n > recv_max) n = recv_max;
    for (int i = 0; i < n; i++)
        recv[i] = mfrc_read_reg(MFRC522_REG_FIFODATA);
    return n;
}

// Poll for a single card and return its 4-byte UID in uid_out.
// Returns true if a card is present.
static bool nfc_poll(uint8_t uid_out[4])
{
    // REQA command
    uint8_t req[1]  = {0x26};
    uint8_t atqa[2] = {0};
    mfrc_write_reg(MFRC522_REG_BITFRAMING, 0x07);   // 7 bits
    if (mfrc_transceive(req, 1, atqa, sizeof(atqa)) < 0) return false;

    // Anti-collision CL1
    mfrc_write_reg(MFRC522_REG_BITFRAMING, 0x00);
    uint8_t anticoll[2] = {0x93, 0x20};
    uint8_t uid_raw[5]  = {0};
    if (mfrc_transceive(anticoll, 2, uid_raw, sizeof(uid_raw)) < 4) return false;

    memcpy(uid_out, uid_raw, 4);
    return true;
}

// ---------------------------------------------------------------------------
// Symbol ID lookup
// ---------------------------------------------------------------------------

static uint8_t uid_to_symbol(const uint8_t uid[4])
{
    for (int i = 0; i < 12; i++) {
        if (memcmp(s_tags[i].uid, uid, 4) == 0)
            return s_tags[i].symbol_id;
    }
    return 0;  // unknown tag
}

// ---------------------------------------------------------------------------
// Buzzer helpers
// ---------------------------------------------------------------------------

static void buzzer_short(void)
{
    gpio_set_level(GPIO_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_BUZZER, 0);
}

static void buzzer_long(void)
{
    gpio_set_level(GPIO_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(800));
    gpio_set_level(GPIO_BUZZER, 0);
}

// ---------------------------------------------------------------------------
// Configuration check
// ---------------------------------------------------------------------------

static void check_configuration(void)
{
    if (s_placed < 12) return;

    bool correct = (memcmp(s_slots, kCorrectOrder, 12) == 0);
    if (correct) {
        s_solved = true;
        gpio_set_level(GPIO_LED_G, 1);

        // Digits 6-7 from correct order first two elements
        uint8_t d6 = kCorrectOrder[0] % 10;   // 7
        uint8_t d7 = kCorrectOrder[1] % 10;   // 2
        uint8_t code[4] = {d6, d7, 0, 0};
        uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)
                         - s_start_ms;
        espnow_slave_notify_solved(code, elapsed);
        buzzer_long();
        ESP_LOGI(TAG, "Solved! Code=%d%d elapsed=%lu ms", d6, d7, elapsed);

    } else {
        gpio_set_level(GPIO_LED_R, 1);
        buzzer_short();
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(GPIO_LED_R, 0);
        // Reset placement
        memset(s_slots, 0, sizeof(s_slots));
        s_placed = 0;
        ESP_LOGW(TAG, "Wrong configuration — reset");
    }
}

// ---------------------------------------------------------------------------
// ESP-NOW command handler
// ---------------------------------------------------------------------------

static void espnow_cmd_handler(espnow_msg_type_t type,
                                const uint8_t *payload, size_t len)
{
    (void)payload; (void)len;
    if (type == MSG_PUZZLE_RESET) {
        memset(s_slots, 0, sizeof(s_slots));
        s_placed = 0;
        s_solved = false;
        gpio_set_level(GPIO_LED_G, 0);
        gpio_set_level(GPIO_LED_R, 0);
        s_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Reset");
    }
    // MSG_PUZZLE_CONFIG not used by P6 (difficulty does not change the tag set)
}

// ---------------------------------------------------------------------------
// NFC polling task
// ---------------------------------------------------------------------------

static void nfc_task(void *arg)
{
    uint8_t uid[4]      = {0};
    uint8_t last_uid[4] = {0};
    uint32_t last_read_ms = 0;

    for (;;) {
        espnow_slave_process();

        if (s_solved) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint32_t t = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (nfc_poll(uid)) {
            // Debounce: ignore same tag within 1 s
            bool same = (memcmp(uid, last_uid, 4) == 0);
            if (!same || (t - last_read_ms) > 1000) {
                uint8_t sym = uid_to_symbol(uid);
                if (sym >= 1 && sym <= 12) {
                    ESP_LOGI(TAG, "Tag placed: symbol %d at position %d",
                             sym, s_placed);
                    buzzer_short();
                    if (s_placed < 12) {
                        s_slots[s_placed++] = sym;
                    }
                    memcpy(last_uid, uid, 4);
                    last_read_ms = t;
                    if (s_placed == 12) check_configuration();
                } else {
                    ESP_LOGW(TAG, "Unknown tag UID %02X:%02X:%02X:%02X",
                             uid[0], uid[1], uid[2], uid[3]);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));   // 5 Hz NFC polling
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi + SPI init
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

static void spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num   = GPIO_MISO,
        .mosi_io_num   = GPIO_MOSI,
        .sclk_io_num   = GPIO_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,   // 10 MHz
        .mode           = 0,
        .spics_io_num   = GPIO_CS,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_ID, &devcfg, &s_spi));

    // MFRC522 reset via RST pin
    gpio_set_direction(GPIO_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    mfrc_reset();
    ESP_LOGI(TAG, "MFRC522 initialized, version=0x%02X",
             mfrc_read_reg(0x37));  // VersionReg
}

static void load_tag_uids_from_nvs(void)
{
    // In production: nvs_open / nvs_get_blob("nfc_tags", s_tags, sizeof(s_tags))
    // For development: pre-populate with known test UIDs
    // Each NTAG213 tag has its symbol_id stored in byte 0 of NDEF block 1
    // Factory programming is done once with tools/nfc/program_tags.py
    memset(s_tags, 0, sizeof(s_tags));
    ESP_LOGW(TAG, "NFC tag UIDs not loaded (NVS empty) — use tools/nfc/program_tags.py");
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "P6 Symboles Alchimiques booting...");

    wifi_init();
    spi_init();
    load_tag_uids_from_nvs();

    // Output GPIOs
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << GPIO_BUZZER) | (1ULL << GPIO_LED_G)
                      | (1ULL << GPIO_LED_R),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);

    // ESP-NOW slave
    static const uint8_t kMasterMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    ESP_ERROR_CHECK(espnow_slave_init(kMasterMac, 6 /* P6 */));
    espnow_slave_register_callback(espnow_cmd_handler);

    s_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    xTaskCreate(nfc_task, "nfc", 4096, NULL, 5, NULL);
}
