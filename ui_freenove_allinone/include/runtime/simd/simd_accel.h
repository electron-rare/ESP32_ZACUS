// simd_accel.h - conversion and DSP helpers with safe scalar fallback.
#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime::simd {

struct SimdAccelStatus {
  bool simd_path_enabled = false;
  bool esp_dsp_enabled = false;
  uint32_t selftest_runs = 0U;
  uint32_t selftest_failures = 0U;
  uint32_t bench_runs = 0U;
  uint32_t bench_loops = 0U;
  uint32_t bench_pixels = 0U;
  uint32_t bench_l8_to_rgb565_us = 0U;
  uint32_t bench_idx8_to_rgb565_us = 0U;
  uint32_t bench_rgb888_to_rgb565_us = 0U;
  uint32_t bench_s16_gain_q15_us = 0U;
};

struct SimdBenchResult {
  uint32_t loops = 0U;
  uint32_t pixels = 0U;
  uint32_t l8_to_rgb565_us = 0U;
  uint32_t idx8_to_rgb565_us = 0U;
  uint32_t rgb888_to_rgb565_us = 0U;
  uint32_t s16_gain_q15_us = 0U;
};

const SimdAccelStatus& status();
void resetBenchStatus();

// --- RGB565 buffer operations ---

/// memcpy wrapper for typed RGB565 buffers.
void simd_rgb565_copy(uint16_t* dst, const uint16_t* src, size_t n_px);

/// Fill n_px pixels with a constant RGB565 color. Uses 32-bit writes when aligned.
void simd_rgb565_fill(uint16_t* dst, uint16_t color565, size_t n_px);

/// Byte-swap every RGB565 pixel (host ↔ big-endian wire order).
/// Optimised: 2 pixels per iteration via 32-bit packing when aligned.
void simd_rgb565_bswap_copy(uint16_t* dst, const uint16_t* src, size_t n_px);

/// Alpha-blend src over dst in-place.
/// alpha = 255 → fully src, alpha = 0 → fully dst.
/// Optimised: 2 pixels per iteration via 32-bit masking when aligned.
void simd_rgb565_alpha_blend(uint16_t* dst, const uint16_t* src,
                             uint8_t alpha, size_t n_px);

/// Scale brightness of each pixel. level = 255 → no change, 0 → black.
/// Optimised: 2 pixels per iteration when aligned.
void simd_rgb565_brightness(uint16_t* dst, const uint16_t* src,
                            uint8_t level, size_t n_px);

// --- Color format conversions ---

/// Grayscale L8 → RGB565 via precomputed LUT. 2-pixel 32-bit fast path when aligned.
void simd_l8_to_rgb565(uint16_t* dst565, const uint8_t* src_l8, size_t n_px);

/// Indexed 8-bit palette → RGB565. 2-pixel 32-bit fast path when aligned.
void simd_index8_to_rgb565(uint16_t* dst565,
                           const uint8_t* idx8,
                           const uint16_t* pal565_256,
                           size_t n_px);

/// Packed RGB888 → RGB565. 32-bit read fast path when src is aligned.
void simd_rgb888_to_rgb565(uint16_t* dst565, const uint8_t* src_rgb888, size_t n_px);

/// YUV422 (YUYV) → RGB565. BT.601 full-range, 2 pixels per iteration.
void simd_yuv422_to_rgb565(uint16_t* dst565, const uint8_t* src_yuv422, size_t n_px);

// --- Audio DSP ---

/// Apply Q15 gain to int16 samples. Uses ESP-DSP when available, scalar fallback.
void simd_s16_gain_q15(int16_t* dst, const int16_t* src, int16_t gain_q15, size_t n);

/// Mix two int16 streams with independent Q15 gains. Uses ESP-DSP when available.
void simd_s16_mix2_q15(int16_t* dst,
                       const int16_t* a,
                       const int16_t* b,
                       int16_t ga_q15,
                       int16_t gb_q15,
                       size_t n);

// --- Self-test and benchmark ---

bool selfTest();
SimdBenchResult runBench(uint32_t loops, uint32_t pixels);

}  // namespace runtime::simd
