#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace core::wav {
inline constexpr uint32_t Rate = 16000, ByteRate = Rate * 2,
                          MaxBytes = 0xffff0000U;
inline constexpr uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
inline constexpr void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = n >> (8 * i);
}
inline constexpr std::array<uint8_t, 44> header(uint32_t bytes) {
  std::array<uint8_t, 44> h{};
  for (unsigned i = 0; i < 4; ++i)
    h[i] = "RIFF"[i];
  put32(h.data() + 4, bytes + 36);
  for (unsigned i = 0; i < 8; ++i)
    h[i + 8] = "WAVEfmt "[i];
  put32(h.data() + 16, 16);
  h[20] = 1;
  h[22] = 1;
  put32(h.data() + 24, Rate);
  put32(h.data() + 28, ByteRate);
  h[32] = 2;
  h[34] = 16;
  for (unsigned i = 0; i < 4; ++i)
    h[i + 36] = "data"[i];
  put32(h.data() + 40, bytes);
  return h;
}
inline constexpr bool differs(const uint8_t *p, const char *text,
                              unsigned size) {
  for (unsigned i = 0; i < size; ++i)
    if (p[i] != text[i])
      return true;
  return false;
}
inline constexpr bool parse(const uint8_t *h, uint64_t fileSize,
                            uint32_t &bytes) {
  if (!h || fileSize < 44 || differs(h, "RIFF", 4) ||
      differs(h + 8, "WAVEfmt ", 8) || read32(h + 16) != 16 || h[20] != 1 ||
      h[21] || h[22] != 1 || h[23] || read32(h + 24) != Rate ||
      read32(h + 28) != ByteRate || h[32] != 2 || h[33] || h[34] != 16 ||
      h[35] || differs(h + 36, "data", 4))
    return false;
  bytes = read32(h + 40);
  return bytes <= MaxBytes && !(bytes & 1) &&
         uint64_t(bytes) + 44 <= fileSize && read32(h + 4) == bytes + 36;
}
} // namespace core::wav
