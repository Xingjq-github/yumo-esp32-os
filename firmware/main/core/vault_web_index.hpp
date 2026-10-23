#pragma once
#include "vault_format.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace vault::interaction {
// Session-local grouping by an existing field. Only entry indices are stored:
// no credential/target copies, no heap allocation and no retained string views.
class EntryGroupIndex {
public:
  static constexpr size_t None = MaxEntries;
  constexpr void clear() {
    m_count = m_groups = 0;
    m_groupFor.fill(None);
  }
  static constexpr int compareKeys(std::string_view a, std::string_view b) {
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
      const char x = fold(a[i]), y = fold(b[i]);
      if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    // Sorting is case-insensitive, grouping remains exact (User != user).
    return a.compare(b);
  }
  template <typename Entries, typename Key>
  constexpr void buildGroups(const Entries &entries, std::string_view category, Key key) {
    clear();
    for (size_t i = 0; i < entries.size() && i < MaxEntries; ++i)
      if (entries[i].category == category) m_order[m_count++] = uint16_t(i);
    std::sort(m_order.begin(), m_order.begin() + m_count, [&](size_t a, size_t b) {
      const int cmp = compareKeys(key(entries[a]), key(entries[b]));
      return cmp ? cmp < 0 : a < b;
    });
    for (size_t i = 0; i < m_count; ++i) {
      if (!i || key(entries[m_order[i]]) != key(entries[m_order[i - 1]]))
        m_starts[m_groups++] = uint16_t(i);
      m_groupFor[m_order[i]] = uint16_t(m_groups - 1);
    }
    m_starts[m_groups] = uint16_t(m_count);
  }
  constexpr size_t groups() const { return m_groups; }
  constexpr size_t count(size_t group) const {
    return group < m_groups ? m_starts[group + 1] - m_starts[group] : 0;
  }
  constexpr size_t entry(size_t group, size_t row = 0) const {
    return row < count(group) ? m_order[m_starts[group] + row] : None;
  }
  constexpr size_t groupFor(size_t entry) const {
    return entry < MaxEntries && m_groups ? m_groupFor[entry] : None;
  }
private:
  static constexpr char fold(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
  std::array<uint16_t, MaxEntries> m_order{}, m_groupFor{};
  std::array<uint16_t, MaxEntries + 1> m_starts{};
  size_t m_count = 0, m_groups = 0;
};
class WebIndex : public EntryGroupIndex {
public:
  static constexpr int compareAccounts(std::string_view a, std::string_view b) {
    return compareKeys(a, b);
  }
  template <typename Entries> constexpr void build(const Entries &entries) {
    buildGroups(entries, "web", [](const auto &entry) -> std::string_view {
      return entry.username;
    });
  }
};
// A single index is reused when entering work/app; an empty optional target is
// still a real group so older credentials never disappear from the lists.
class TargetIndex : public EntryGroupIndex {
public:
  template <typename Entries>
  constexpr void build(const Entries &entries, std::string_view category) {
    if (category != "work" && category != "app") {
      clear();
      return;
    }
    buildGroups(entries, category, [](const auto &entry) -> std::string_view {
      return entry.target;
    });
  }
};
} // namespace vault::interaction
