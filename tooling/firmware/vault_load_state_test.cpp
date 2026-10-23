// Tests the production loading state machine, not a separate reimplementation.
#include "core/vault_load_state.hpp"
using vault::LoadState;
using vault::StartResult;
using Phase = LoadState::Phase;
constexpr bool freshOpen() {
  LoadState s;
  s.reset(1234);
  return s.due(1234) && !s.ready() && !s.expired(1234);
}
constexpr bool busyBackoff() {
  LoadState s;
  s.reset(1000);
  s.attempted(StartResult::Busy, 1000);
  return s.phase() == Phase::Waiting && !s.due(1100) && !s.due(1499) &&
         s.due(1500) && !s.expired(1500);
}
constexpr bool repeatedBusyIsBounded() {
  LoadState s;
  s.reset(1000);
  for (uint32_t now = 1000; now < 11000; now += 500) {
    if (!s.due(now) || s.expired(now)) return false;
    s.attempted(StartResult::Busy, now);
  }
  return s.due(11000) && s.expired(11000);
}
constexpr bool noAutomaticRetry(StartResult result) {
  LoadState s;
  s.reset(100);
  s.attempted(result, 100);
  return s.phase() == Phase::Failed && !s.ready() && !s.due(200) &&
         !s.due(10100) && !s.due(60100);
}
constexpr bool manualRecovery() {
  LoadState s;
  s.reset(1);
  s.attempted(StartResult::NoMemory, 1);
  s.reset(20000);
  if (!s.due(20000)) return false;
  s.attempted(StartResult::Started, 20000);
  if (s.phase() != Phase::Reading || s.due(80000)) return false;
  s.finish(true);
  return s.ready() && !s.due(90000);
}
constexpr bool busyThenStarted() {
  LoadState s;
  s.reset(0);
  s.attempted(StartResult::Busy, 0);
  s.attempted(StartResult::Started, 500);
  return s.phase() == Phase::Reading && !s.ready() && !s.due(1000) &&
         !s.expired(20000);
}
constexpr bool readFailureOrCancel() {
  LoadState s;
  s.reset(0);
  s.attempted(StartResult::Started, 0);
  s.finish(false);
  return s.phase() == Phase::Failed && !s.ready() && !s.due(60000);
}
constexpr bool lockOrReopen(Phase phase) {
  LoadState s;
  s.reset(0);
  if (phase == Phase::Waiting) s.attempted(StartResult::Busy, 0);
  if (phase == Phase::Reading) s.attempted(StartResult::Started, 0);
  if (phase == Phase::Ready) s.finish(true);
  if (phase == Phase::Failed) s.finish(false);
  s.reset(30000);
  return s.phase() == Phase::Pending && !s.ready() && s.due(30000) &&
         !s.expired(30000);
}
constexpr bool tickWrap() {
  LoadState s;
  constexpr uint32_t start = UINT32_MAX - 100;
  s.reset(start);
  s.attempted(StartResult::Busy, start);
  return !s.due(398) && s.due(399) && !s.expired(9898) && s.expired(9899);
}
static_assert(freshOpen());
static_assert(busyBackoff());
static_assert(repeatedBusyIsBounded());
static_assert(noAutomaticRetry(StartResult::NoMemory));
static_assert(noAutomaticRetry(StartResult::Unavailable));
static_assert(manualRecovery());
static_assert(busyThenStarted());
static_assert(readFailureOrCancel());
static_assert(lockOrReopen(Phase::Waiting));
static_assert(lockOrReopen(Phase::Reading));
static_assert(lockOrReopen(Phase::Ready));
static_assert(lockOrReopen(Phase::Failed));
static_assert(tickWrap());
