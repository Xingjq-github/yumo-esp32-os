#pragma once
#include <cstdint>
namespace core::fc {
// Monotonic active wall time, independent of restored frame counters and RTC.
// Loading, pause menus, SD writes and stopped games never advance this clock.
class AutoSaveClock {
public:
    static constexpr uint64_t IntervalUs=60ULL*1000000;
    constexpr void update(int64_t now,bool active) {
        if(running_ && now>last_) {
            const auto delta=static_cast<uint64_t>(now-last_);
            elapsed_=delta>=IntervalUs-elapsed_?IntervalUs:elapsed_+delta;
        }
        last_=now;running_=active;
    }
    constexpr bool due() const {return elapsed_>=IntervalUs;}
    // A new game, successful load or completed attempt gets a full new minute.
    // Failed attempts do not cause retries every frame or a catch-up burst.
    constexpr void reset(int64_t now) {elapsed_=0;last_=now;running_=false;}
private:
    uint64_t elapsed_=0;
    int64_t last_=0;
    bool running_=false;
};
} // namespace core::fc
