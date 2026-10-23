#pragma once
#include "hid_input_report.hpp"
#include "../../components/nofrendo/nes/ines.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
namespace core::fc {
enum Key { A, B, Select, Start, Up, Down, Left, Right, Pause, Exit, KeyCount };
struct Mapping {
  std::array<uint8_t, KeyCount> usage{0x1d, 0x1b, 0x2b, 0x28, 0x52,
                                      0x51, 0x50, 0x4f, 0x2c, 0x29};
  constexpr uint8_t pad(const sys::hidinput::Keys &keys) const {
    uint8_t out = 0;
    for (unsigned i = 0; i < 8; ++i)
      if (keys.down(usage[i]))
        out |= 1u << i;
    if ((out & 0x30) == 0x30)
      out &= ~0x30;
    if ((out & 0xc0) == 0xc0)
      out &= ~0xc0;
    return out;
  }
};
inline constexpr uint8_t keyUsage(std::string key) {
  for (char &ch : key)
    if (ch >= 'a' && ch <= 'z')
      ch -= 32;
  if (key.size() == 1) {
    const char c = key[0];
    if (c >= 'A' && c <= 'Z')
      return c - 'A' + 4;
    if (c >= '1' && c <= '9')
      return c - '1' + 0x1e;
    if (c == '0')
      return 0x27;
  }
  if (key == "UP")
    return 0x52;
  if (key == "DOWN")
    return 0x51;
  if (key == "LEFT")
    return 0x50;
  if (key == "RIGHT")
    return 0x4f;
  if (key == "ENTER")
    return 0x28;
  if (key == "ESC" || key == "ESCAPE")
    return 0x29;
  if (key == "TAB")
    return 0x2b;
  if (key == "SPACE")
    return 0x2c;
  if (key == "BACKSPACE")
    return 0x2a;
  if (key.size() >= 2 && key[0] == 'F') {
    unsigned n = 0;
    for (size_t i = 1; i < key.size(); ++i) {
      if (key[i] < '0' || key[i] > '9')
        return 0;
      n = n * 10 + key[i] - '0';
      if (n > 12)
        return 0;
    }
    if (n)
      return 0x39 + n;
  }
  return 0;
}
inline constexpr bool supported(unsigned mapper) {
  return ines_mapper_supported(mapper);
}
inline constexpr bool headerValid(const uint8_t *h, size_t size,
                                  unsigned &mapper) {
  ines_info_t info{};
  const auto error=ines_parse(h,size,&info);
  mapper=info.mapper;
  return error==INES_OK;
}
} // namespace core::fc
