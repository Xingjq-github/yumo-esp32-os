#pragma once
#include "vault_format.hpp"
#include <array>

namespace vault::interaction {
// Only indices live here; credentials remain in the unlocked session.
class ListWindow {
public:
  static constexpr int RowHeight = 42, Pitch = 43, ViewHeight = 203;
  // Six possibly visible rows plus one buffer row on either side.
  static constexpr size_t Slots = 8, None = MaxEntries;
  constexpr void clear() { m_count = 0; }
  constexpr bool append(size_t entry) {
    if (m_count == MaxEntries || entry >= MaxEntries)
      return false;
    m_entries[m_count++] = static_cast<uint16_t>(entry);
    return true;
  }
  constexpr size_t count() const { return m_count; }
  constexpr size_t entry(size_t row) const {
    return row < m_count ? m_entries[row] : None;
  }
  constexpr size_t slots() const { return m_count < Slots ? m_count : Slots; }
  constexpr int height(int pitch = Pitch) const { return m_count ? int(m_count) * pitch - 1 : 0; }
  constexpr int clamp(int y, int viewport = ViewHeight, int pitch = Pitch) const {
    const int end = height(pitch) > viewport ? height(pitch) - viewport : 0;
    return y < 0 ? 0 : y > end ? end : y;
  }
  constexpr size_t first(int y, int viewport = ViewHeight, int pitch = Pitch) const {
    const size_t top = size_t(clamp(y, viewport, pitch) / pitch);
    const size_t start = top ? top - 1 : 0;
    const size_t last = m_count - slots();
    return start < last ? start : last;
  }
  static constexpr size_t slot(size_t row) { return row % Slots; }
private:
  std::array<uint16_t, MaxEntries> m_entries{};
  size_t m_count = 0;
};
} // namespace vault::interaction
