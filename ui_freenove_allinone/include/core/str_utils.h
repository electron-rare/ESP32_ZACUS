// str_utils.h - shared string utilities (zero-dependency, header-only).
#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>

namespace core {

/// Case-insensitive string comparison. Returns true if lhs == rhs ignoring case.
/// Safe with nullptr: (nullptr, nullptr) → false.
inline bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  for (size_t i = 0U;; ++i) {
    const char a = lhs[i];
    const char b = rhs[i];
    if (a == '\0' && b == '\0') {
      return true;
    }
    if (a == '\0' || b == '\0') {
      return false;
    }
    if (std::tolower(static_cast<unsigned char>(a)) !=
        std::tolower(static_cast<unsigned char>(b))) {
      return false;
    }
  }
}

/// Safe bounded string copy. Always null-terminates dst.
/// No-op if dst is nullptr or dst_size is 0.
inline void copyText(char* dst, size_t dst_size, const char* src) {
  if (dst == nullptr || dst_size == 0U) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dst_size - 1U);
  dst[dst_size - 1U] = '\0';
}

}  // namespace core
