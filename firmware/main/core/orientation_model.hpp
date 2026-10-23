#pragma once
#include <cmath>
#include <cstdint>

namespace sys::orientation {
constexpr uint32_t FreshMs=250, StableMs=800, CooldownMs=1200;

// Screen-down gravity determines the next quarter turn; flat, diagonal,
// shaking and stale samples cannot select a direction. State is RAM-only.
class Filter {
public:
    void reset() {primed=false;pending=false;seen=false;}
    unsigned current() const {return applied;}
    void committed(unsigned turns,uint32_t now) {applied=turns;lastChange=now;reset();}
    void failed(uint32_t now) {lastChange=now;reset();}
    int update(float x,float y,float z,uint32_t sampledAt,uint32_t now,bool allowed) {
        if(!allowed || uint32_t(now-sampledAt)>FreshMs ||
           !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)) {reset();return -1;}
        if(seen && sampledAt==lastSample)return -1;
        if(seen && uint32_t(sampledAt-lastSample)>FreshMs)reset();
        seen=true;lastSample=sampledAt;
        const float norm=x*x+y*y+z*z;
        if(norm<0.36f || norm>1.96f || std::fabs(z)>0.85f) {reset();return -1;}
        if(!primed) {fx=x;fy=y;primed=true;}
        else {fx=fx*0.75f+x*0.25f;fy=fy*0.75f+y*0.25f;}
        const float ax=std::fabs(fx),ay=std::fabs(fy);
        int target=-1;
        if(ax>0.65f && ax>ay+0.20f)target=fx>0?3:1;
        else if(ay>0.65f && ay>ax+0.20f)target=fy>0?0:2;
        if(target<0 || unsigned(target)==applied) {pending=false;return -1;}
        if(!pending || candidate!=unsigned(target)) {candidate=unsigned(target);since=now;pending=true;return -1;}
        if(uint32_t(now-since)<StableMs || uint32_t(now-lastChange)<CooldownMs)return -1;
        return target; // Caller commits only after all hardware commands succeed.
    }
private:
    unsigned applied=0,candidate=0;
    uint32_t lastChange=0,since=0,lastSample=0;
    float fx=0,fy=0;
    bool primed=false,pending=false,seen=false;
};
}
