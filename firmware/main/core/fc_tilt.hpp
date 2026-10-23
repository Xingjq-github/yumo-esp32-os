#pragma once
#include "fc_controls.hpp"
#include <cstdint>

namespace core::fc::tilt {
// Fixed horizontal reference; these are g components, with no trigonometry or
// per-frame allocations. Separate enter/release levels suppress hand tremor.
constexpr float Enter=0.10f, Release=0.055f, Filter=0.65f;
constexpr uint32_t MaxSampleAgeMs=150;
constexpr bool fresh(uint32_t now,uint32_t sampledAt) {return uint32_t(now-sampledAt)<=MaxSampleAgeMs;}
constexpr int axis(float value,int previous) {
    if(value>=Enter) return 1;
    if(value<=-Enter) return -1;
    if(previous>0 && value>Release) return 1;
    if(previous<0 && value<-Release) return -1;
    return 0;
}
struct State {
    float x=0,y=0;
    int horizontal=0,vertical=0;
    uint8_t pad=0;
    bool seeded=false;
    constexpr void clear() {x=y=0;horizontal=vertical=0;pad=0;seeded=false;}
    constexpr bool update(float ax,float ay,float az) {
        const float norm=ax*ax+ay*ay+az*az;
        // Comparisons also reject NaN/infinity, free fall and large shocks.
        if(!(norm>=0.25f && norm<=2.25f)) {clear();return false;}
        if(!seeded) {x=ax;y=ay;seeded=true;}
        else {x+=(ax-x)*Filter;y+=(ay-y)*Filter;}
        horizontal=axis(x,horizontal);vertical=axis(y,vertical);
        // Existing hardware/display mapping: +X right, +Y down.
        pad=(horizontal>0?controls::Right:horizontal<0?controls::Left:0) |
            (vertical>0?controls::Down:vertical<0?controls::Up:0);
        return true;
    }
};
constexpr uint8_t compose(bool enabled,uint8_t gravity,uint8_t touch,bool dragging,uint8_t keyboard) {
    // Touching the stick (even its centre) overrides gravity. Select/Start and
    // physical A/B remain independent; keyboard direction keeps per-axis priority.
    const uint8_t local=enabled && !dragging?uint8_t((touch&0x0f)|(gravity&0xf0)):touch;
    return controls::combine(keyboard,local);
}
}
