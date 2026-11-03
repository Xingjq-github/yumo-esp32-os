#pragma once
#include <cstdint>

namespace ui::protection {
constexpr uint32_t IdleMs=120000;
constexpr uint32_t MoveMs=15000;
// Only real input or ineligible scenes restart this timer; rendering never does.
class Idle {
    uint32_t since=0,seen=0;
    bool initialized=false;
public:
    constexpr bool step(uint32_t now,uint32_t activity,bool allowed) {
        if(!initialized || !allowed || activity!=seen) {
            initialized=true;since=now;seen=activity;
            return false;
        }
        return uint32_t(now-since)>=IdleMs;
    }
    constexpr void reset() {initialized=false;}
};
struct Point {int x,y;};
constexpr Point position(uint32_t elapsed) {
    constexpr Point stops[]={{16,22},{72,22},{128,22},{128,102},
                             {128,182},{72,182},{16,182},{16,102}};
    return stops[(elapsed/MoveMs)%8];
}
// The entire first touch belongs to dismissal, even after the cover is removed.
class TouchGate {
    bool held=false;
public:
    constexpr bool filter(bool covered,bool pressed) {
        const bool consume=held || (covered && pressed);
        held=consume && pressed;
        return consume;
    }
};
}
