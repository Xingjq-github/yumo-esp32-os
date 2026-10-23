#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace core::stopwatch {
// All times are monotonic microseconds, independent of wall-clock/NTP changes.
struct Lap { uint32_t number=0; uint64_t splitUs=0,totalUs=0; };
class Model {
public:
    static constexpr size_t Capacity=99;
    bool running() const {return active;}
    uint64_t elapsed(int64_t now) const {
        if(now<lastAction)now=lastAction;
        return accumulated+(active && now>startedAt?uint64_t(now-startedAt):0);
    }
    uint64_t split(int64_t now) const {return elapsed(now)-lastLap;}
    size_t size() const {return used;}
    uint32_t count() const {return recorded;}
    const Lap &newest(size_t index) const {return laps[(next+Capacity-1-index)%Capacity];}
    void start(int64_t now) {now=stamp(now);if(!active){startedAt=now;active=true;}}
    void pause(int64_t now) {now=stamp(now);if(active){accumulated=elapsed(now);active=false;}}
    void reset() {active=false;accumulated=lastLap=0;startedAt=0;used=next=recorded=0;}
    void record(int64_t now) {
        if(!active || recorded==UINT32_MAX)return;
        now=stamp(now);
        const auto total=elapsed(now);
        laps[next]={++recorded,total-lastLap,total};lastLap=total;
        next=(next+1)%Capacity;if(used<Capacity)++used;
    }
private:
    bool active=false;
    int64_t startedAt=0,lastAction=0;
    uint64_t accumulated=0,lastLap=0;
    size_t used=0,next=0;
    uint32_t recorded=0;
    std::array<Lap,Capacity> laps{};
    int64_t stamp(int64_t now) {if(now>lastAction)lastAction=now;return lastAction;}
};
inline void format(char *out,size_t size,uint64_t us,bool fraction=true) {
    const auto cs=us/10000,seconds=cs/100,hours=seconds/3600;
    const auto minutes=unsigned((seconds/60)%60),sec=unsigned(seconds%60),centi=unsigned(cs%100);
    if(hours)std::snprintf(out,size,fraction?"%02llu:%02u:%02u.%02u":"%02llu:%02u:%02u",
                           static_cast<unsigned long long>(hours),minutes,sec,centi);
    else std::snprintf(out,size,fraction?"%02u:%02u.%02u":"%02u:%02u",minutes,sec,centi);
}
}
