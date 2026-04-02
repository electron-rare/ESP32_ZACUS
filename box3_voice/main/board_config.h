#pragma once

/*
 * ESP32-S3-BOX-3 Hardware Pin Configuration
 * Reference: Espressif ESP-BOX-3 Schematic v1.2
 */

/* ---------- I2S Audio (ES8311 codec) ---------- */
#define BOX3_I2S_MCLK       16
#define BOX3_I2S_BCLK        9
#define BOX3_I2S_WS         45
#define BOX3_I2S_DOUT       15   /* Speaker data out */
#define BOX3_I2S_DIN        10   /* Microphone data in */
#define BOX3_PA_ENABLE      46   /* Power amplifier enable (active high) */

/* ---------- I2C Bus ---------- */
#define BOX3_I2C_SCL        18
#define BOX3_I2C_SDA         8
#define BOX3_I2C_NUM         I2C_NUM_0
#define BOX3_I2C_FREQ_HZ    400000

/* ---------- ES8311 Audio Codec ---------- */
#define BOX3_ES8311_ADDR    0x18

/* ---------- Display (ILI9341) ---------- */
#define BOX3_LCD_WIDTH      320
#define BOX3_LCD_HEIGHT     240

/* ---------- Audio Parameters ---------- */
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BITS          16
#define AUDIO_CHANNELS      1
#define AUDIO_FRAME_MS      20
#define AUDIO_FRAME_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000)
