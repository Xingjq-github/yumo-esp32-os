#include "core/vault_list.hpp"
using W = vault::interaction::ListWindow;

constexpr bool boundaries() {
  W w;
  if (w.count() || w.slots() || w.height() || w.first(100) ||
      w.entry(0) != W::None || w.clamp(-1)) return false;
  if (w.append(vault::MaxEntries)) return false;
  for (size_t i = 0; i < vault::MaxEntries; ++i)
    if (!w.append(i)) return false;
  if (w.append(0) || w.count() != vault::MaxEntries || w.slots() != 8)
    return false;
  if (w.clamp(99999) != w.height() - W::ViewHeight ||
      w.first(99999) != vault::MaxEntries - W::Slots) return false;
  w.clear();
  return !w.count() && !w.height() && !w.slots() && w.entry(0) == W::None;
}
constexpr bool categoryMapping() {
  W w;
  // Noncontiguous session indices must remain the click target, not row numbers.
  for (size_t category = 0; category < 4; ++category) {
    w.clear();
    for (size_t i = 0; i < vault::MaxEntries; ++i)
      if (i % 4 == category && !w.append(i)) return false;
    if (w.count() != 64) return false;
    for (size_t row = 0; row < w.count(); ++row)
      if (w.entry(row) != row * 4 + category) return false;
  }
  w.clear(); // a smaller replacement session cannot expose old indices
  return w.append(2) && w.entry(0) == 2 && w.entry(1) == W::None &&
         w.first(9000) == 0 && w.clamp(9000) == 0;
}
constexpr bool visibleRowsCovered(size_t count, int viewport) {
  W w;
  for (size_t i = 0; i < count; ++i) w.append(i);
  // Exercise every pixel near row boundaries, including both overscroll ends.
  for (int y = -2; y < w.height() + 2; ++y) {
    const int top = w.clamp(y, viewport);
    const size_t first = w.first(y, viewport);
    if (first + w.slots() > count) return false;
    bool used[W::Slots]{};
    for (size_t row = first; row < first + w.slots(); ++row) {
      if (used[W::slot(row)] || w.entry(row) != row) return false;
      used[W::slot(row)] = true;
    }
    const size_t visibleFirst = size_t(top / W::Pitch);
    size_t visibleEnd = size_t((top + viewport + W::Pitch - 1) / W::Pitch);
    if (visibleEnd > count) visibleEnd = count;
    if (count && (first > visibleFirst || first + w.slots() < visibleEnd))
      return false;
  }
  return true;
}
constexpr bool reuse() {
  W w;
  for (size_t i = 0; i < 256; ++i) w.append(i);
  for (int y = 2 * W::Pitch; y < w.height() - 10 * W::Pitch; y += W::Pitch) {
    const size_t before = w.first(y), after = w.first(y + W::Pitch);
    if (after != before + 1 || W::slot(before) != W::slot(after + W::Slots - 1))
      return false;
  }
  return true;
}
static_assert(boundaries());
static_assert(categoryMapping());
static_assert(visibleRowsCovered(0, 203));
static_assert(visibleRowsCovered(1, 203));
static_assert(visibleRowsCovered(5, 203));
static_assert(visibleRowsCovered(8, 203));
static_assert(visibleRowsCovered(9, 203));
static_assert(visibleRowsCovered(128, 203));
static_assert(visibleRowsCovered(256, 203));
static_assert(visibleRowsCovered(256, 160));
static_assert(reuse());
