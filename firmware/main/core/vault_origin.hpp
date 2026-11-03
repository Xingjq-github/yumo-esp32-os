#pragma once
#include <string>

namespace vault {
namespace origin_detail {
inline bool whitespace(unsigned char c) {
  return c == ' ' || (c >= '\t' && c <= '\r');
}
inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline bool letter(char c) { return c >= 'a' && c <= 'z'; }

inline bool host(const std::string &value, size_t begin, size_t end) {
  if (begin == end || end - begin > 253)
    return false;
  size_t label = begin;
  bool numeric = true;
  for (size_t i = begin; i <= end; ++i) {
    if (i == end || value[i] == '.') {
      if (i == label || i - label > 63 || value[label] == '-' ||
          value[i - 1] == '-')
        return false;
      label = i + 1;
    } else {
      const char c = value[i];
      if (!letter(c) && !digit(c) && c != '-')
        return false;
      if (!digit(c))
        numeric = false;
    }
  }
  if (!numeric)
    return true; // ASCII DNS name (including local single-label hosts).

  // Numeric hosts must be unambiguous dotted-decimal IPv4. Reject shortened,
  // octal-looking and out-of-range forms before passing them to the transport.
  unsigned parts = 0;
  label = begin;
  for (size_t i = begin; i <= end; ++i) {
    if (i != end && value[i] != '.')
      continue;
    if (i - label > 3 || (i - label > 1 && value[label] == '0'))
      return false;
    unsigned part = 0;
    for (size_t j = label; j < i; ++j)
      part = part * 10 + unsigned(value[j] - '0');
    if (part > 255)
      return false;
    ++parts;
    label = i + 1;
  }
  return parts == 4;
}
} // namespace origin_detail

// Only a service origin is accepted: HTTP(S), ASCII DNS/IPv4 and an optional
// port. A root slash and surrounding ASCII whitespace are safe to normalize;
// paths, credentials and URL suffixes are rejected rather than discarded.
// Invalid input leaves output intact, including when input and output alias.
inline bool normalizeOrigin(const std::string &input, std::string &output) {
  size_t begin = 0, end = input.size();
  while (begin < end && origin_detail::whitespace(input[begin]))
    ++begin;
  while (end > begin && origin_detail::whitespace(input[end - 1]))
    --end;
  if (end == begin || end - begin > 240)
    return false;
  std::string value = input.substr(begin, end - begin);
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z')
      c = char(c + ('a' - 'A'));
  }
  const size_t prefix = value.compare(0, 7, "http://") == 0 ? 7
                        : value.compare(0, 8, "https://") == 0 ? 8
                                                               : 0;
  if (!prefix || value.size() <= prefix)
    return false;
  if (value.back() == '/')
    value.pop_back();
  const size_t colon = value.find(':', prefix);
  const size_t hostEnd = colon == std::string::npos ? value.size() : colon;
  if (!origin_detail::host(value, prefix, hostEnd))
    return false;
  if (colon != std::string::npos) {
    const size_t digits = value.size() - colon - 1;
    if (!digits || digits > 5)
      return false;
    unsigned port = 0;
    for (size_t i = colon + 1; i < value.size(); ++i) {
      if (!origin_detail::digit(value[i]))
        return false;
      port = port * 10 + unsigned(value[i] - '0');
    }
    if (!port || port > 65535)
      return false;
    value.erase(colon + 1);
    value += std::to_string(port);
  }
  output.swap(value);
  return true;
}
} // namespace vault
