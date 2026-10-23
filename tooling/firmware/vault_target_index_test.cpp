#include "core/vault_web_index.hpp"
#include "core/vault_list.hpp"

using T = vault::interaction::TargetIndex;
using W = vault::interaction::ListWindow;
struct TargetMetadata { std::string_view category, username, target; };

constexpr bool targetGroupsKeepEveryAccount() {
  const std::array<TargetMetadata, 12> entries{{
    {"work", "same", "Team"}, {"app", "same", "Team"},
    {"work", "other", "Team"}, {"work", "same", "Portal"},
    {"work", "missing", ""}, {"work", "same", "team"},
    {"app", "same", "Mobile"}, {"work", "other", ""},
    {"web", "same", "Team"}, {"other", "same", "Team"},
    {"work", "same", "单位 / 系统"}, {"work", "other", "单位 / 系统"}
  }};
  T index;index.build(entries, "work");
  const auto team = index.groupFor(0), empty = index.groupFor(4), chinese = index.groupFor(10);
  if (index.groups() != 5 || index.groupFor(2) != team || index.count(team) != 2 ||
      index.entry(team) != 0 || index.entry(team, 1) != 2 ||
      team == index.groupFor(3) || team == index.groupFor(5) ||
      empty != index.groupFor(7) || index.count(empty) != 2 ||
      chinese != index.groupFor(11) || index.count(chinese) != 2 ||
      index.groupFor(1) != T::None || index.groupFor(6) != T::None ||
      index.groupFor(8) != T::None || index.groupFor(9) != T::None) return false;
  W groups, accounts;
  for (size_t g = 0; g < index.groups(); ++g) groups.append(index.entry(g));
  for (size_t i = 0; i < index.count(team); ++i) accounts.append(index.entry(team, i));
  if (groups.count() != 5 || groups.entry(team) != 0 || accounts.count() != 2 ||
      accounts.entry(1) != 2) return false;
  // The same optional target in another category must never merge with work.
  index.build(entries, "app");
  if (index.groups() != 2 || index.entry(index.groupFor(1)) != 1 ||
      index.groupFor(0) != T::None || index.groupFor(11) != T::None) return false;
  index.build(entries, "web");
  return !index.groups() && index.groupFor(1) == T::None && index.entry(0) == T::None;
}

constexpr bool targetOffsetsSurviveCategorySwitch() {
  const std::array<TargetMetadata, 5> entries{{
    {"work", "a", "Zeta"}, {"app", "a", "App"},
    {"work", "b", "Beta"}, {"app", "b", "App"}, {"work", "c", "Zeta"}
  }};
  T index;std::array<int32_t, vault::MaxEntries> offsets{};
  index.build(entries, "work");
  offsets[index.entry(index.groupFor(0))] = 86;
  offsets[index.entry(index.groupFor(2))] = 43;
  index.build(entries, "app");
  const auto app = index.entry(index.groupFor(1));
  if (offsets[app] != 0) return false;
  offsets[app] = 129;
  index.build(entries, "work");
  if (offsets[index.entry(index.groupFor(0))] != 86 ||
      offsets[index.entry(index.groupFor(2))] != 43 || index.groupFor(1) != T::None) return false;
  index.build(entries, "app");
  if (offsets[index.entry(index.groupFor(1))] != 129) return false;
  const std::array<TargetMetadata, 0> empty{};
  index.build(empty, "app");
  return !index.groups() && !index.count(0) && index.groupFor(1) == T::None;
}

constexpr bool targetLargeGroupStaysBounded() {
  std::array<TargetMetadata, vault::MaxEntries + 1> entries{};
  for (auto &entry : entries) entry = {"work", "same", "Shared"};
  T index;index.build(entries, "work");
  if (index.groups() != 1 || index.count(0) != vault::MaxEntries ||
      index.groupFor(vault::MaxEntries) != T::None) return false;
  W accounts;
  for (size_t i = 0; i < vault::MaxEntries; ++i) {
    if (index.entry(0, i) != i || index.groupFor(i) != 0) return false;
    accounts.append(index.entry(0, i));
  }
  if (accounts.slots() != 8 || accounts.entry(255) != 255 || accounts.first(99999) != 248)
    return false;
  index.clear();
  return !index.groups() && index.groupFor(255) == T::None && index.entry(0) == T::None;
}

constexpr bool targetAllDistinctGroups() {
  std::array<std::array<char, 3>, 256> names{};
  std::array<TargetMetadata, 256> entries{};
  for (size_t i = 0; i < 256; ++i) {
    names[i][0] = char('A' + i / 16);names[i][1] = char('A' + i % 16);
    entries[255 - i] = {"app", "same", std::string_view(names[i].data(), 2)};
  }
  T index;index.build(entries, "app");
  if (index.groups() != 256) return false;
  for (size_t i = 0; i < 256; ++i)
    if (index.count(i) != 1 || index.entry(i) != 255 - i || index.groupFor(255 - i) != i)
      return false;
  return index.count(256) == 0 && index.entry(255, 1) == T::None;
}

static_assert(targetGroupsKeepEveryAccount());
static_assert(targetOffsetsSurviveCategorySwitch());
static_assert(targetLargeGroupStaysBounded());
static_assert(targetAllDistinctGroups());
