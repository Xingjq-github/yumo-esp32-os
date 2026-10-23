#pragma once
#include <cstdint>
namespace vault {
enum class StartResult { Started, Busy, Unavailable, NoMemory };
// UI-thread only. Retry a retiring worker, never spin on allocation/read errors.
class LoadState {
public:
  enum class Phase { Pending, Waiting, Reading, Ready, Failed };
  static constexpr uint32_t RetryMs = 500, WaitLimitMs = 10000;
  constexpr void reset(uint32_t now) {
    m_phase = Phase::Pending;
    m_since = m_lastAttempt = now;
  }
  constexpr Phase phase() const { return m_phase; }
  constexpr bool ready() const { return m_phase == Phase::Ready; }
  constexpr bool expired(uint32_t now) const {
    return m_phase == Phase::Waiting && uint32_t(now - m_since) >= WaitLimitMs;
  }
  constexpr bool due(uint32_t now) const {
    return m_phase == Phase::Pending ||
           (m_phase == Phase::Waiting && uint32_t(now - m_lastAttempt) >= RetryMs);
  }
  constexpr void attempted(StartResult result, uint32_t now) {
    m_lastAttempt = now;
    m_phase = result == StartResult::Started ? Phase::Reading
              : result == StartResult::Busy ? Phase::Waiting
                                            : Phase::Failed;
  }
  constexpr void finish(bool ok) { m_phase = ok ? Phase::Ready : Phase::Failed; }
private:
  Phase m_phase = Phase::Pending;
  uint32_t m_since = 0, m_lastAttempt = 0;
};
} // namespace vault
