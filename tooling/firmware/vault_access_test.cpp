#include "core/vault_interaction.hpp"
#include "core/vault_list.hpp"
using namespace vault::interaction;
constexpr bool accessLifecycle() {
  AccessState a;
  if (a.super() || a.visible(0) || a.visible(AccessState::None)) return false;
  if (a.complete(true, 0) || a.complete(true, AccessState::Global)) return false;
  a.request(0);
  if (a.visible(0) || a.complete(false, 0) || a.visible(0)) return false;
  a.request(0);
  if (a.complete(true, 1) || a.complete(true, 0)) return false;
  a.request(0);
  if (!a.complete(true, 0) || !a.visible(0) || a.visible(1) || a.super()) return false;
  a.leavePage();
  if (a.visible(0) || a.complete(true, 0)) return false;
  a.request(0); a.lock();
  if (a.complete(true, 0) || a.visible(0)) return false;
  a.request(AccessState::Global);
  if (!a.complete(true, AccessState::Global)) return false;
  a.leavePage();
  if (!a.super() || !a.visible(0) || !a.visible(255)) return false;
  a.lock();
  if (a.super() || a.visible(0) || a.complete(true, AccessState::Global)) return false;
  a.request(AccessState::Global); a.lock();
  return !a.complete(true, AccessState::Global) && !a.super();
}
constexpr bool purposeWindows() {
  using P = PurposeLayout;
  ListWindow w;
  if (w.height(P::Pitch) || w.first(0, P::ViewHeight, P::Pitch)) return false;
  for (size_t n = 1; n <= vault::MaxEntries; ++n) {
    if (!w.append(vault::MaxEntries - n)) return false;
    if (w.slots() > 8 || w.height(P::Pitch) != int(n) * P::Pitch - 1) return false;
    // Start, middle and bottom viewports, with a partially visible final row.
    for (int y : {0, w.height(P::Pitch) / 2, w.height(P::Pitch) + 100}) {
      const int top = w.clamp(y, P::ViewHeight, P::Pitch);
      const size_t first = w.first(y, P::ViewHeight, P::Pitch);
      for (size_t row = top / P::Pitch; row < n && int(row) * P::Pitch < top + P::ViewHeight; ++row) {
        if (row < first || row >= first + w.slots()) return false;
        if (w.entry(row) != vault::MaxEntries - row - 1) return false;
      }
      for (size_t row = first; row < first + w.slots(); ++row)
        for (size_t next = row + 1; next < first + w.slots(); ++next)
          if (w.slot(row) == w.slot(next)) return false;
    }
  }
  return !w.append(0);
}
static_assert(accessLifecycle(), "Grants are verified, scoped and revoked on lock/leave");
static_assert(purposeWindows(), "256 credentials remain reachable through eight reused rows");
static_assert(PurposeLayout::InputHeight >= 36 && PurposeLayout::InputWidth >= 36);
static_assert(PurposeLayout::NameHeight + PurposeLayout::InputHeight + 2 + PurposeLayout::ViewHeight == 203);
static_assert(PurposeLayout::InputWidth + PurposeLayout::TextWidth + 1 == 240);
static_assert(36 + 97 + 40 + 64 + 3 == 240, "Purpose header fits screen");
