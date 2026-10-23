#pragma once
// Shared, bounded JYVAULT2 framing. No heap allocation or platform
// dependencies.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace vault {
constexpr size_t HeaderSize = 128, MaxBundle = 262144, MaxEntries = 256;
constexpr size_t MaxNoteBytes = 256;
constexpr uint32_t Rounds = 0, PinRounds = 10000;
inline uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
inline void write32(uint8_t *p, uint32_t x) {
  for (int i = 0; i < 4; ++i)
    p[i] = uint8_t(x >> (i * 8));
}
inline bool nonzero(const uint8_t *p, size_t n) {
  uint8_t v = 0;
  while (n--)
    v |= *p++;
  return v != 0;
}
inline bool framing(const uint8_t *p, size_t n) {
  return p && n >= 144 && n <= MaxBundle && !std::memcmp(p, "JYVAULT2", 8) &&
         read32(p + 8) == Rounds && read32(p + 12) == n - 128 &&
         read32(p + 20) == 0 && nonzero(p + 24, 16) && nonzero(p + 40, 16);
}
constexpr bool validPin(const char *p, size_t n) {
  if (!p || n < 4 || n > 12)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (p[i] < '0' || p[i] > '9')
      return false;
  return true;
}
inline bool printable(const char *p, size_t n) {
  if (!p && n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (uint8_t(p[i]) < 32 || uint8_t(p[i]) > 126)
      return false;
  return true;
}
inline void wipe(void *p, size_t n) {
  volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
  while (n--)
    *v++ = 0;
}
inline bool equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t diff = 0;
  while (n--)
    diff |= *a++ ^ *b++;
  return diff == 0;
}
} // namespace vault
