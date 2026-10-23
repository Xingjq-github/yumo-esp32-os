#pragma once
#include "file_operations.hpp"
#include <string>
#include <cstdint>
namespace sys::lanweb {
inline constexpr uint64_t MaxUpload = 256ULL * 1024 * 1024;
inline int hexDigit(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
inline bool decode(const std::string &value, std::string &out) {
  out.clear();
  for (size_t i = 0; i < value.size(); ++i) {
    unsigned char c = value[i];
    if (c == '%') {
      if (i + 2 >= value.size()) return false;
      int a = hexDigit(value[++i]), b = hexDigit(value[++i]);
      if (a < 0 || b < 0) return false;
      c = (a << 4) | b;
    }
    if (c < 32 || c == 127) return false;
    out += static_cast<char>(c);
  }
  return true;
}
inline bool path(const std::string &relative, std::string &absolute, bool root = false) {
  if (relative.empty() || relative[0] != '/') return false;
  absolute = "/sdcard" + (relative == "/" ? "" : relative);
  return core::files::validPath("/sdcard", absolute, root);
}
inline bool cookieMatches(const std::string &cookie, const std::string &token) {
  if (token.size() != 64) return false;
  size_t pos = 0;
  while (pos < cookie.size()) {
    while (pos < cookie.size() && cookie[pos] == ' ') ++pos;
    const auto end = cookie.find(';', pos);
    const auto part = cookie.substr(pos, end == std::string::npos ? end : end - pos);
    if (part.rfind("yumo_session=", 0) == 0) {
      if (part.size() != 13 + token.size()) return false;
      unsigned difference = 0;
      for (size_t i = 0; i < token.size(); ++i) difference |= part[13 + i] ^ token[i];
      return difference == 0;
    }
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return false;
}
}
