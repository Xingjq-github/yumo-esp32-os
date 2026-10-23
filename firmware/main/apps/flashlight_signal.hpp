#pragma once
#include <cstdint>
namespace apps::torch {
inline constexpr int kLevels[]={5,30,60,100};
inline constexpr uint32_t kUnitMs=180;
// SOS is the continuous nine-element distress prosign ...---..., not three
// separately spaced letters: 1-unit element gaps; 7-unit gap before repetition.
inline constexpr uint8_t kUnits[]={1,1,1,1,1,1,3,1,3,1,3,1,1,1,1,1,1,7};
constexpr uint32_t cycleUnits() { uint32_t n=0; for(auto u:kUnits) n+=u; return n; }
inline constexpr uint32_t kCycleMs=cycleUnits()*kUnitMs;
constexpr uint32_t untilEdge(uint32_t elapsedMs) {
    auto phase=elapsedMs%kCycleMs;
    for(auto units:kUnits) {const auto span=units*kUnitMs;if(phase<span)return span-phase;phase-=span;}
    return kUnitMs;
}
constexpr bool litAt(uint32_t elapsedMs) {
    auto phase=elapsedMs%kCycleMs;
    for(unsigned i=0;i<sizeof(kUnits);++i) {
        const auto duration=kUnits[i]*kUnitMs;
        if(phase<duration) return i%2==0;
        phase-=duration;
    }
    return false;
}
static_assert(cycleUnits()==30 && kCycleMs==5400);
static_assert(litAt(0) && !litAt(180) && litAt(5400));
static_assert(kLevels[0]==5 && kLevels[3]==100);
} // namespace apps::torch
