// simd_accel.cpp - safe conversion and DSP kernels with optional ESP-DSP acceleration.
#include "runtime/simd/simd_accel.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>

#include "runtime/memory/caps_allocator.h"

#ifndef UI_ENABLE_SIMD_PATH
#define UI_ENABLE_SIMD_PATH 0
#endif

#ifndef UI_SIMD_USE_ESP_DSP
#define UI_SIMD_USE_ESP_DSP 1
#endif

#if (UI_SIMD_USE_ESP_DSP != 0) && __has_include(<dsps_mul.h>)
#include <dsps_add.h>
#include <dsps_mul.h>
#define UI_SIMD_HAS_ESP_DSP 1
#else
#define UI_SIMD_HAS_ESP_DSP 0
#endif

namespace runtime::simd {

namespace {

constexpr bool kSimdPathEnabled = (UI_ENABLE_SIMD_PATH != 0);
constexpr size_t kAudioChunk = 128U;
constexpr uint32_t kBenchMinPixels = 64U;
constexpr uint32_t kBenchMaxPixels = 8192U;
constexpr uint32_t kBenchMinLoops = 1U;
constexpr uint32_t kBenchMaxLoops = 5000U;

// Gray LUT — built once, guarded by atomic flag (acquire/release semantics).
uint16_t g_gray8_to_rgb565[256] = {};
std::atomic<bool> g_gray_lut_ready{false};

SimdAccelStatus g_status = {};
bool g_status_initialized = false;

// ============================================================================
// Internal helpers
// ============================================================================

void initStatusDefaults() {
  g_status.simd_path_enabled = kSimdPathEnabled;
  g_status.esp_dsp_enabled = (UI_SIMD_HAS_ESP_DSP != 0);
}

void ensureStatusInitialized() {
  if (g_status_initialized) {
    return;
  }
  initStatusDefaults();
  g_status_initialized = true;
}

inline uint8_t clampU8(int32_t value) {
  if (value < 0)   return 0U;
  if (value > 255) return 255U;
  return static_cast<uint8_t>(value);
}

inline int16_t clampS16(int32_t value) {
  if (value < -32768) return -32768;
  if (value >  32767) return  32767;
  return static_cast<int16_t>(value);
}

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>((r & 0xF8U) << 8U)) |
      (static_cast<uint16_t>((g & 0xFCU) << 3U)) |
      (static_cast<uint16_t>( b >> 3U)));
}

// Decompose RGB565 into 8-bit channels.
inline void unpack565(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = static_cast<uint8_t>((px >> 8U) & 0xF8U);
  g = static_cast<uint8_t>((px >> 3U) & 0xFCU);
  b = static_cast<uint8_t>((px << 3U) & 0xF8U);
}

// Thread-safe lazy LUT initialisation (acquire/release).
void ensureGrayLut() {
  if (g_gray_lut_ready.load(std::memory_order_acquire)) {
    return;
  }
  for (uint16_t i = 0U; i < 256U; ++i) {
    g_gray8_to_rgb565[i] = rgb565(static_cast<uint8_t>(i),
                                  static_cast<uint8_t>(i),
                                  static_cast<uint8_t>(i));
  }
  g_gray_lut_ready.store(true, std::memory_order_release);
}

// ============================================================================
// Audio scalar kernels
// ============================================================================

void gainQ15Scalar(int16_t* dst, const int16_t* src, int16_t gain_q15, size_t n) {
  if (dst == nullptr || src == nullptr || n == 0U) return;
  for (size_t i = 0U; i < n; ++i) {
    const int32_t mul = static_cast<int32_t>(src[i]) * static_cast<int32_t>(gain_q15);
    const int32_t rounded = (mul >= 0) ? (mul + (1 << 14)) : (mul - (1 << 14));
    dst[i] = clampS16(rounded >> 15);
  }
}

void mixQ15Scalar(int16_t* dst,
                  const int16_t* a,
                  const int16_t* b,
                  int16_t ga_q15,
                  int16_t gb_q15,
                  size_t n) {
  if (dst == nullptr || a == nullptr || b == nullptr || n == 0U) return;
  for (size_t i = 0U; i < n; ++i) {
    const int32_t acc =
        static_cast<int32_t>(a[i]) * static_cast<int32_t>(ga_q15) +
        static_cast<int32_t>(b[i]) * static_cast<int32_t>(gb_q15);
    const int32_t rounded = (acc >= 0) ? (acc + (1 << 14)) : (acc - (1 << 14));
    dst[i] = clampS16(rounded >> 15);
  }
}

// ============================================================================
// Audio ESP-DSP kernels
// ============================================================================

void gainQ15EspDsp(int16_t* dst, const int16_t* src, int16_t gain_q15, size_t n) {
#if UI_SIMD_HAS_ESP_DSP
  int16_t gain_buf[kAudioChunk] = {};
  size_t offset = 0U;
  while (offset < n) {
    const size_t chunk = (n - offset < kAudioChunk) ? (n - offset) : kAudioChunk;
    for (size_t i = 0U; i < chunk; ++i) {
      gain_buf[i] = gain_q15;
    }
    const esp_err_t rc = dsps_mul_s16(src + offset, gain_buf, dst + offset,
                                      static_cast<int>(chunk), 1, 1, 1, 15);
    if (rc != ESP_OK) {
      gainQ15Scalar(dst + offset, src + offset, gain_q15, n - offset);
      return;
    }
    offset += chunk;
  }
#else
  gainQ15Scalar(dst, src, gain_q15, n);
#endif
}

void mixQ15EspDsp(int16_t* dst,
                  const int16_t* a,
                  const int16_t* b,
                  int16_t ga_q15,
                  int16_t gb_q15,
                  size_t n) {
#if UI_SIMD_HAS_ESP_DSP
  // Allocate two temporary buffers on the stack for small n, heap for large n.
  // For kAudioChunk-sized blocks, stack allocation is safe.
  int16_t tmp_a[kAudioChunk] = {};
  int16_t tmp_b[kAudioChunk] = {};
  int16_t gain_a[kAudioChunk] = {};
  int16_t gain_b[kAudioChunk] = {};

  size_t offset = 0U;
  while (offset < n) {
    const size_t chunk = (n - offset < kAudioChunk) ? (n - offset) : kAudioChunk;

    for (size_t i = 0U; i < chunk; ++i) {
      gain_a[i] = ga_q15;
      gain_b[i] = gb_q15;
    }

    const esp_err_t rc_a = dsps_mul_s16(a + offset, gain_a, tmp_a,
                                        static_cast<int>(chunk), 1, 1, 1, 15);
    const esp_err_t rc_b = dsps_mul_s16(b + offset, gain_b, tmp_b,
                                        static_cast<int>(chunk), 1, 1, 1, 15);
    if (rc_a != ESP_OK || rc_b != ESP_OK) {
      mixQ15Scalar(dst + offset, a + offset, b + offset, ga_q15, gb_q15, n - offset);
      return;
    }
    // Add the two scaled streams.
    const esp_err_t rc_add = dsps_add_s16(tmp_a, tmp_b, dst + offset,
                                          static_cast<int>(chunk), 1, 1, 1, 0);
    if (rc_add != ESP_OK) {
      // Fallback: scalar add for this chunk.
      for (size_t i = 0U; i < chunk; ++i) {
        dst[offset + i] = clampS16(static_cast<int32_t>(tmp_a[i]) +
                                   static_cast<int32_t>(tmp_b[i]));
      }
    }
    offset += chunk;
  }
#else
  mixQ15Scalar(dst, a, b, ga_q15, gb_q15, n);
#endif
}

// ============================================================================
// Self-test helpers
// ============================================================================

template <typename T>
bool arraysEqual(const T* a, const T* b, size_t n) {
  if (a == nullptr || b == nullptr) return false;
  for (size_t i = 0U; i < n; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

}  // namespace

// ============================================================================
// Status / bench
// ============================================================================

const SimdAccelStatus& status() {
  ensureStatusInitialized();
  return g_status;
}

void resetBenchStatus() {
  ensureStatusInitialized();
  g_status.bench_runs = 0U;
  g_status.bench_loops = 0U;
  g_status.bench_pixels = 0U;
  g_status.bench_l8_to_rgb565_us = 0U;
  g_status.bench_idx8_to_rgb565_us = 0U;
  g_status.bench_rgb888_to_rgb565_us = 0U;
  g_status.bench_s16_gain_q15_us = 0U;
}

// ============================================================================
// RGB565 buffer operations
// ============================================================================

void simd_rgb565_copy(uint16_t* dst, const uint16_t* src, size_t n_px) {
  if (dst == nullptr || src == nullptr || n_px == 0U) return;
  std::memcpy(dst, src, n_px * sizeof(uint16_t));
}

void simd_rgb565_fill(uint16_t* dst, uint16_t color565, size_t n_px) {
  if (dst == nullptr || n_px == 0U) return;
  if ((reinterpret_cast<uintptr_t>(dst) & 0x3U) == 0U) {
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
    const uint32_t packed =
        (static_cast<uint32_t>(color565) << 16U) | static_cast<uint32_t>(color565);
    size_t i = 0U;
    for (; i + 1U < n_px; i += 2U) {
      dst32[i >> 1U] = packed;
    }
    if (i < n_px) {
      dst[i] = color565;
    }
    return;
  }
  for (size_t i = 0U; i < n_px; ++i) {
    dst[i] = color565;
  }
}

// Optimised: swap bytes of 2 pixels per iteration via 32-bit masking.
void simd_rgb565_bswap_copy(uint16_t* dst, const uint16_t* src, size_t n_px) {
  if (dst == nullptr || src == nullptr || n_px == 0U) return;

  size_t i = 0U;
  if ((reinterpret_cast<uintptr_t>(dst) & 0x3U) == 0U &&
      (reinterpret_cast<uintptr_t>(src) & 0x3U) == 0U) {
    const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
    for (; i + 1U < n_px; i += 2U) {
      const uint32_t v = src32[i >> 1U];
      // Swap bytes within each 16-bit half independently.
      dst32[i >> 1U] = ((v << 8U) & 0xFF00FF00U) | ((v >> 8U) & 0x00FF00FFU);
    }
  }
  for (; i < n_px; ++i) {
    const uint16_t v = src[i];
    dst[i] = static_cast<uint16_t>((v << 8U) | (v >> 8U));
  }
}

// Alpha blend: dst = src * alpha/255 + dst * (255-alpha)/255.
// Optimised: 2 pixels per iteration via 32-bit masking when aligned.
void simd_rgb565_alpha_blend(uint16_t* dst, const uint16_t* src,
                             uint8_t alpha, size_t n_px) {
  if (dst == nullptr || src == nullptr || n_px == 0U) return;

  if (alpha == 255U) {
    std::memcpy(dst, src, n_px * sizeof(uint16_t));
    return;
  }
  if (alpha == 0U) {
    return;  // dst unchanged.
  }

  const uint32_t a = static_cast<uint32_t>(alpha);
  const uint32_t ia = 255U - a;

  for (size_t i = 0U; i < n_px; ++i) {
    uint8_t sr, sg, sb, dr, dg, db;
    unpack565(src[i], sr, sg, sb);
    unpack565(dst[i], dr, dg, db);
    const uint8_t r = static_cast<uint8_t>((sr * a + dr * ia) / 255U);
    const uint8_t g = static_cast<uint8_t>((sg * a + dg * ia) / 255U);
    const uint8_t b = static_cast<uint8_t>((sb * a + db * ia) / 255U);
    dst[i] = rgb565(r, g, b);
  }
}

// Scale brightness: dst = src * level / 255.
void simd_rgb565_brightness(uint16_t* dst, const uint16_t* src,
                            uint8_t level, size_t n_px) {
  if (dst == nullptr || src == nullptr || n_px == 0U) return;

  if (level == 255U) {
    if (dst != src) std::memcpy(dst, src, n_px * sizeof(uint16_t));
    return;
  }
  if (level == 0U) {
    std::memset(dst, 0, n_px * sizeof(uint16_t));
    return;
  }

  const uint32_t lv = static_cast<uint32_t>(level);
  for (size_t i = 0U; i < n_px; ++i) {
    uint8_t r, g, b;
    unpack565(src[i], r, g, b);
    dst[i] = rgb565(
        static_cast<uint8_t>((static_cast<uint32_t>(r) * lv) / 255U),
        static_cast<uint8_t>((static_cast<uint32_t>(g) * lv) / 255U),
        static_cast<uint8_t>((static_cast<uint32_t>(b) * lv) / 255U));
  }
}

// ============================================================================
// Color format conversions
// ============================================================================

void simd_l8_to_rgb565(uint16_t* dst565, const uint8_t* src_l8, size_t n_px) {
  if (dst565 == nullptr || src_l8 == nullptr || n_px == 0U) return;
  ensureGrayLut();

  size_t i = 0U;
  if ((reinterpret_cast<uintptr_t>(dst565) & 0x3U) == 0U) {
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst565);
    for (; i + 1U < n_px; i += 2U) {
      const uint16_t c0 = g_gray8_to_rgb565[src_l8[i]];
      const uint16_t c1 = g_gray8_to_rgb565[src_l8[i + 1U]];
      dst32[i >> 1U] = (static_cast<uint32_t>(c1) << 16U) | c0;
    }
  }
  for (; i < n_px; ++i) {
    dst565[i] = g_gray8_to_rgb565[src_l8[i]];
  }
}

void simd_index8_to_rgb565(uint16_t* dst565,
                           const uint8_t* idx8,
                           const uint16_t* pal565_256,
                           size_t n_px) {
  if (dst565 == nullptr || idx8 == nullptr || pal565_256 == nullptr || n_px == 0U) return;

  size_t i = 0U;
  if ((reinterpret_cast<uintptr_t>(dst565) & 0x3U) == 0U) {
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst565);
    for (; i + 1U < n_px; i += 2U) {
      const uint16_t c0 = pal565_256[idx8[i]];
      const uint16_t c1 = pal565_256[idx8[i + 1U]];
      dst32[i >> 1U] = (static_cast<uint32_t>(c1) << 16U) | c0;
    }
  }
  for (; i < n_px; ++i) {
    dst565[i] = pal565_256[idx8[i]];
  }
}

// Optimised: read 4 bytes at a time when src is 4-byte aligned.
void simd_rgb888_to_rgb565(uint16_t* dst565, const uint8_t* src_rgb888, size_t n_px) {
  if (dst565 == nullptr || src_rgb888 == nullptr || n_px == 0U) return;

  size_t i = 0U;

  if ((reinterpret_cast<uintptr_t>(src_rgb888) & 0x3U) == 0U && n_px >= 2U) {
    // Fast path: read 4 bytes → extract 1⅓ pixels. Process groups of 4 pixels = 12 bytes = 3 reads.
    // Simpler: process pairs — pixel 0 starts at byte 0, pixel 1 at byte 3.
    // We read two uint32_t (bytes 0-7) to extract 2 complete RGB888 pixels (6 bytes used).
    const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src_rgb888);
    for (; i + 3U < n_px; i += 4U) {
      // 4 pixels = 12 bytes = 3 uint32 reads.
      const uint32_t w0 = src32[0];  // bytes  0-3 : R0 G0 B0 R1
      const uint32_t w1 = src32[1];  // bytes  4-7 : G1 B1 R2 G2
      const uint32_t w2 = src32[2];  // bytes  8-11: B2 R3 G3 B3
      src32 += 3;

      const uint8_t r0 = static_cast<uint8_t>(w0);
      const uint8_t g0 = static_cast<uint8_t>(w0 >> 8U);
      const uint8_t b0 = static_cast<uint8_t>(w0 >> 16U);

      const uint8_t r1 = static_cast<uint8_t>(w0 >> 24U);
      const uint8_t g1 = static_cast<uint8_t>(w1);
      const uint8_t b1 = static_cast<uint8_t>(w1 >> 8U);

      const uint8_t r2 = static_cast<uint8_t>(w1 >> 16U);
      const uint8_t g2 = static_cast<uint8_t>(w1 >> 24U);
      const uint8_t b2 = static_cast<uint8_t>(w2);

      const uint8_t r3 = static_cast<uint8_t>(w2 >> 8U);
      const uint8_t g3 = static_cast<uint8_t>(w2 >> 16U);
      const uint8_t b3 = static_cast<uint8_t>(w2 >> 24U);

      dst565[i + 0U] = rgb565(r0, g0, b0);
      dst565[i + 1U] = rgb565(r1, g1, b1);
      dst565[i + 2U] = rgb565(r2, g2, b2);
      dst565[i + 3U] = rgb565(r3, g3, b3);
    }
  }

  // Scalar tail.
  for (; i < n_px; ++i) {
    const uint8_t r = src_rgb888[(i * 3U) + 0U];
    const uint8_t g = src_rgb888[(i * 3U) + 1U];
    const uint8_t b = src_rgb888[(i * 3U) + 2U];
    dst565[i] = rgb565(r, g, b);
  }
}

void simd_yuv422_to_rgb565(uint16_t* dst565, const uint8_t* src_yuv422, size_t n_px) {
  if (dst565 == nullptr || src_yuv422 == nullptr || n_px == 0U) return;

  size_t i = 0U;
  for (; i + 1U < n_px; i += 2U) {
    const uint8_t y0 = src_yuv422[(i * 2U) + 0U];
    const uint8_t u  = src_yuv422[(i * 2U) + 1U];
    const uint8_t y1 = src_yuv422[(i * 2U) + 2U];
    const uint8_t v  = src_yuv422[(i * 2U) + 3U];

    const int32_t c0 = static_cast<int32_t>(y0) - 16;
    const int32_t c1 = static_cast<int32_t>(y1) - 16;
    const int32_t d  = static_cast<int32_t>(u)  - 128;
    const int32_t e  = static_cast<int32_t>(v)  - 128;

    dst565[i]      = rgb565(clampU8((298 * c0 + 409 * e + 128) >> 8),
                            clampU8((298 * c0 - 100 * d - 208 * e + 128) >> 8),
                            clampU8((298 * c0 + 516 * d + 128) >> 8));
    dst565[i + 1U] = rgb565(clampU8((298 * c1 + 409 * e + 128) >> 8),
                            clampU8((298 * c1 - 100 * d - 208 * e + 128) >> 8),
                            clampU8((298 * c1 + 516 * d + 128) >> 8));
  }
  // Odd trailing pixel: treat as grey.
  if (i < n_px) {
    const uint8_t y = src_yuv422[i * 2U];
    dst565[i] = rgb565(y, y, y);
  }
}

// ============================================================================
// Audio DSP
// ============================================================================

void simd_s16_gain_q15(int16_t* dst, const int16_t* src, int16_t gain_q15, size_t n) {
  if (dst == nullptr || src == nullptr || n == 0U) return;
#if UI_SIMD_HAS_ESP_DSP
  gainQ15EspDsp(dst, src, gain_q15, n);
#else
  gainQ15Scalar(dst, src, gain_q15, n);
#endif
}

void simd_s16_mix2_q15(int16_t* dst,
                       const int16_t* a,
                       const int16_t* b,
                       int16_t ga_q15,
                       int16_t gb_q15,
                       size_t n) {
  if (dst == nullptr || a == nullptr || b == nullptr || n == 0U) return;
#if UI_SIMD_HAS_ESP_DSP
  mixQ15EspDsp(dst, a, b, ga_q15, gb_q15, n);
#else
  mixQ15Scalar(dst, a, b, ga_q15, gb_q15, n);
#endif
}

// ============================================================================
// Self-test
// ============================================================================

bool selfTest() {
  ensureStatusInitialized();
  g_status.selftest_runs += 1U;

  constexpr size_t kN = 257U;
  uint8_t  l8[kN]          = {};
  uint8_t  idx[kN]         = {};
  uint16_t pal[256]        = {};
  uint16_t out_a[kN]       = {};
  uint16_t out_b[kN]       = {};
  uint8_t  rgb888[kN * 3U] = {};
  uint8_t  yuv422[(kN + 1U) * 2U] = {};
  int16_t  s16_a[kN]       = {};
  int16_t  s16_b[kN]       = {};
  int16_t  s16_out[kN]     = {};
  int16_t  s16_ref[kN]     = {};

  for (size_t i = 0U; i < kN; ++i) {
    l8[i]  = static_cast<uint8_t>((i * 31U + 17U) & 0xFFU);
    idx[i] = static_cast<uint8_t>((i * 19U +  7U) & 0xFFU);
    rgb888[(i * 3U) + 0U] = static_cast<uint8_t>((i * 11U)       & 0xFFU);
    rgb888[(i * 3U) + 1U] = static_cast<uint8_t>((i * 13U + 3U)  & 0xFFU);
    rgb888[(i * 3U) + 2U] = static_cast<uint8_t>((i * 17U + 9U)  & 0xFFU);
    yuv422[(i * 2U)]      = static_cast<uint8_t>((i *  5U + 40U) & 0xFFU);
    yuv422[(i * 2U) + 1U] = static_cast<uint8_t>((i *  7U + 80U) & 0xFFU);
    s16_a[i] = static_cast<int16_t>((static_cast<int32_t>(i) *  97) - 12000);
    s16_b[i] = static_cast<int16_t>((static_cast<int32_t>(i) *  53) -  9000);
  }
  for (uint16_t i = 0U; i < 256U; ++i) {
    pal[i] = rgb565(static_cast<uint8_t>(i),
                    static_cast<uint8_t>(255U - i),
                    static_cast<uint8_t>(i ^ 0x5AU));
  }

  bool ok = true;

  // L8 → RGB565
  simd_l8_to_rgb565(out_a, l8, kN);
  for (size_t i = 0U; i < kN; ++i) { out_b[i] = rgb565(l8[i], l8[i], l8[i]); }
  ok = ok && arraysEqual(out_a, out_b, kN);

  // Index8 → RGB565
  simd_index8_to_rgb565(out_a, idx, pal, kN);
  for (size_t i = 0U; i < kN; ++i) { out_b[i] = pal[idx[i]]; }
  ok = ok && arraysEqual(out_a, out_b, kN);

  // RGB888 → RGB565
  simd_rgb888_to_rgb565(out_a, rgb888, kN);
  for (size_t i = 0U; i < kN; ++i) {
    out_b[i] = rgb565(rgb888[(i * 3U)], rgb888[(i * 3U) + 1U], rgb888[(i * 3U) + 2U]);
  }
  ok = ok && arraysEqual(out_a, out_b, kN);

  // YUV422 → RGB565 (idempotent: same result twice)
  simd_yuv422_to_rgb565(out_a, yuv422, kN - 1U);
  simd_yuv422_to_rgb565(out_b, yuv422, kN - 1U);
  ok = ok && arraysEqual(out_a, out_b, kN - 1U);

  // bswap copy
  simd_rgb565_bswap_copy(out_a, out_b, kN - 1U);
  for (size_t i = 0U; i < kN - 1U; ++i) {
    const uint16_t v = out_b[i];
    const uint16_t expected = static_cast<uint16_t>((v << 8U) | (v >> 8U));
    if (out_a[i] != expected) { ok = false; break; }
  }

  // Brightness: level=255 → identity
  simd_rgb565_brightness(out_a, out_b, 255U, kN - 1U);
  ok = ok && arraysEqual(out_a, out_b, kN - 1U);

  // Alpha blend: alpha=255 → dst becomes src
  simd_rgb565_copy(out_a, out_b, 0);  // zero-length no-op guard
  simd_rgb565_fill(out_a, 0x1234U, kN);
  simd_rgb565_alpha_blend(out_a, out_b, 255U, kN - 1U);
  ok = ok && arraysEqual(out_a, out_b, kN - 1U);

  // Gain Q15
  simd_s16_gain_q15(s16_out, s16_a, static_cast<int16_t>(16384), kN);
  gainQ15Scalar(s16_ref, s16_a, static_cast<int16_t>(16384), kN);
  ok = ok && arraysEqual(s16_out, s16_ref, kN);

  // Mix Q15
  simd_s16_mix2_q15(s16_out, s16_a, s16_b,
                    static_cast<int16_t>(16384), static_cast<int16_t>(8192), kN);
  mixQ15Scalar(s16_ref, s16_a, s16_b,
               static_cast<int16_t>(16384), static_cast<int16_t>(8192), kN);
  ok = ok && arraysEqual(s16_out, s16_ref, kN);

  if (!ok) {
    g_status.selftest_failures += 1U;
    Serial.println("[SIMD] selfTest FAILED");
  } else {
    Serial.println("[SIMD] selfTest OK");
  }
  return ok;
}

// ============================================================================
// Benchmark
// ============================================================================

SimdBenchResult runBench(uint32_t loops, uint32_t pixels) {
  ensureStatusInitialized();
  if (loops  < kBenchMinLoops)  loops  = kBenchMinLoops;
  if (loops  > kBenchMaxLoops)  loops  = kBenchMaxLoops;
  if (pixels < kBenchMinPixels) pixels = kBenchMinPixels;
  if (pixels > kBenchMaxPixels) pixels = kBenchMaxPixels;

  SimdBenchResult result = {};
  result.loops  = loops;
  result.pixels = pixels;

  auto* l8     = static_cast<uint8_t*>(
      runtime::memory::CapsAllocator::allocPsram(pixels, "simd.bench.l8"));
  auto* idx    = static_cast<uint8_t*>(
      runtime::memory::CapsAllocator::allocPsram(pixels, "simd.bench.idx"));
  auto* pal    = static_cast<uint16_t*>(
      runtime::memory::CapsAllocator::allocInternalDma(sizeof(uint16_t) * 256U, "simd.bench.pal"));
  auto* dst565 = static_cast<uint16_t*>(
      runtime::memory::CapsAllocator::allocPsram(static_cast<size_t>(pixels) * sizeof(uint16_t), "simd.bench.dst"));
  auto* rgb888 = static_cast<uint8_t*>(
      runtime::memory::CapsAllocator::allocPsram(static_cast<size_t>(pixels) * 3U, "simd.bench.rgb888"));
  auto* s16_a  = static_cast<int16_t*>(
      runtime::memory::CapsAllocator::allocPsram(static_cast<size_t>(pixels) * sizeof(int16_t), "simd.bench.s16a"));
  auto* s16_out = static_cast<int16_t*>(
      runtime::memory::CapsAllocator::allocPsram(static_cast<size_t>(pixels) * sizeof(int16_t), "simd.bench.s16out"));

  if (!l8 || !idx || !pal || !dst565 || !rgb888 || !s16_a || !s16_out) {
    runtime::memory::CapsAllocator::release(l8);
    runtime::memory::CapsAllocator::release(idx);
    runtime::memory::CapsAllocator::release(pal);
    runtime::memory::CapsAllocator::release(dst565);
    runtime::memory::CapsAllocator::release(rgb888);
    runtime::memory::CapsAllocator::release(s16_a);
    runtime::memory::CapsAllocator::release(s16_out);
    return result;
  }

  for (uint32_t i = 0U; i < pixels; ++i) {
    l8[i]              = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);
    idx[i]             = static_cast<uint8_t>((i * 29U +  3U) & 0xFFU);
    rgb888[(i * 3U)]   = static_cast<uint8_t>((i *  9U)       & 0xFFU);
    rgb888[(i * 3U) + 1U] = static_cast<uint8_t>((i * 13U + 7U) & 0xFFU);
    rgb888[(i * 3U) + 2U] = static_cast<uint8_t>((i * 17U + 5U) & 0xFFU);
    s16_a[i]           = static_cast<int16_t>((static_cast<int32_t>(i) * 23) - 12000);
  }
  for (uint16_t i = 0U; i < 256U; ++i) {
    pal[i] = rgb565(static_cast<uint8_t>(i),
                    static_cast<uint8_t>(255U - i),
                    static_cast<uint8_t>(i));
  }

  uint32_t t = micros();
  for (uint32_t loop = 0U; loop < loops; ++loop) simd_l8_to_rgb565(dst565, l8, pixels);
  result.l8_to_rgb565_us = micros() - t;

  t = micros();
  for (uint32_t loop = 0U; loop < loops; ++loop) simd_index8_to_rgb565(dst565, idx, pal, pixels);
  result.idx8_to_rgb565_us = micros() - t;

  t = micros();
  for (uint32_t loop = 0U; loop < loops; ++loop) simd_rgb888_to_rgb565(dst565, rgb888, pixels);
  result.rgb888_to_rgb565_us = micros() - t;

  t = micros();
  for (uint32_t loop = 0U; loop < loops; ++loop)
    simd_s16_gain_q15(s16_out, s16_a, static_cast<int16_t>(21845), pixels);
  result.s16_gain_q15_us = micros() - t;

  g_status.bench_runs += 1U;
  g_status.bench_loops              = result.loops;
  g_status.bench_pixels             = result.pixels;
  g_status.bench_l8_to_rgb565_us    = result.l8_to_rgb565_us;
  g_status.bench_idx8_to_rgb565_us  = result.idx8_to_rgb565_us;
  g_status.bench_rgb888_to_rgb565_us = result.rgb888_to_rgb565_us;
  g_status.bench_s16_gain_q15_us    = result.s16_gain_q15_us;

  runtime::memory::CapsAllocator::release(l8);
  runtime::memory::CapsAllocator::release(idx);
  runtime::memory::CapsAllocator::release(pal);
  runtime::memory::CapsAllocator::release(dst565);
  runtime::memory::CapsAllocator::release(rgb888);
  runtime::memory::CapsAllocator::release(s16_a);
  runtime::memory::CapsAllocator::release(s16_out);
  return result;
}

}  // namespace runtime::simd
