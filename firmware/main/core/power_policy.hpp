#pragma once
#include <array>
#include <cstdint>

namespace sys::power {
enum Hint : uint32_t { None=0, StayLit=1, LowLoad=2, Dim=4, KeepSession=8 };
enum class Stage : uint8_t { Active, Quiet, Dark, Standby };
struct Config {
    uint16_t screenSeconds=60, quietSeconds=60, standbySeconds=120, deepSeconds=240;
    uint8_t brightness=15, automatic=1, longest=1, protectLinks=0;
    uint8_t sleepOnPower=0, screenOffOnPower=0;
};
constexpr uint16_t seconds(uint16_t n) { return n==0?0:n<10?10:n>3600?3600:n; }
constexpr Config normalize(Config c) {
    c.screenSeconds=seconds(c.screenSeconds);c.quietSeconds=seconds(c.quietSeconds);
    c.standbySeconds=seconds(c.standbySeconds);c.deepSeconds=seconds(c.deepSeconds);
    if(c.deepSeconds && c.standbySeconds && c.deepSeconds<=c.standbySeconds)
        c.deepSeconds=c.standbySeconds<3600?c.standbySeconds+10:0;
    c.brightness=c.brightness<5?5:c.brightness>100?100:c.brightness;
    c.automatic=!!c.automatic;c.longest=!!c.longest;c.protectLinks=!!c.protectLinks;
    c.sleepOnPower=!!c.sleepOnPower;c.screenOffOnPower=!!c.screenOffOnPower;
    return c;
}
using LegacyEncoded=std::array<uint16_t,9>;
using Encoded=std::array<uint16_t,11>;
constexpr Encoded encode(Config c) {
    return {2,c.screenSeconds,c.quietSeconds,c.standbySeconds,c.deepSeconds,
            c.brightness,c.automatic,c.longest,c.protectLinks,c.sleepOnPower,c.screenOffOnPower};
}
constexpr Config decode(const Encoded &v) {
    if(v[0]!=2 || v[5]>255 || v[6]>1 || v[7]>1 || v[8]>1 || v[9]>1 || v[10]>1)return {};
    return normalize({v[1],v[2],v[3],v[4],uint8_t(v[5]),uint8_t(v[6]),uint8_t(v[7]),uint8_t(v[8]),uint8_t(v[9]),uint8_t(v[10])});
}
constexpr Config decodeLegacy(const LegacyEncoded &v) {
    if(v[0]!=1)return {};
    return decode({2,v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],0,0});
}
struct Input {
    uint32_t now=0,activity=0,hints=0;
    bool screen=true,displayBusy=false,sleepBusy=false,connected=false;
    bool supplyProtected=false; // external power OR hardware cannot rule it out
};
struct Decision {
    Stage stage=Stage::Active;
    bool black=false,deep=false,radiosOff=false;
};
// GUI-owned policy. Unsigned elapsed times remain correct over the 49-day wrap.
// Only real input updates activity; rendering, radio heartbeats and clock ticks do not.
class Policy {
    bool initialized=false,wasOn=true,wasProtected=false,wasSleepAllowed=false,wasDeepAllowed=false;
    uint32_t seenActivity=0,seenHints=0,idleAt=0,darkAt=0,deepAt=0;
public:
    constexpr Decision step(Config c,const Input &in) {
        c=normalize(c);
        const bool sleepAllowed=!in.supplyProtected || c.sleepOnPower;
        const bool timedSleep=c.automatic && sleepAllowed;
        const bool timedDeep=timedSleep && c.deepSeconds && !(c.protectLinks && in.connected);
        if(!initialized) {
            initialized=true;wasOn=in.screen;wasProtected=in.supplyProtected;seenActivity=in.activity;seenHints=in.hints;
            idleAt=darkAt=deepAt=in.now;
            wasSleepAllowed=timedSleep;wasDeepAllowed=timedDeep;
        }
        if(in.activity!=seenActivity || in.hints!=seenHints) {
            seenActivity=in.activity;seenHints=in.hints;idleAt=in.now;
        }
        if(in.screen!=wasOn) {wasOn=in.screen;idleAt=darkAt=deepAt=in.now;}
        if(in.supplyProtected!=wasProtected) {
            wasProtected=in.supplyProtected;idleAt=in.now;
        }
        if(timedSleep!=wasSleepAllowed) {wasSleepAllowed=timedSleep;darkAt=in.now;}
        if(timedDeep!=wasDeepAllowed) {wasDeepAllowed=timedDeep;deepAt=in.now;}
        const bool screenAllowed=!in.supplyProtected || c.screenOffOnPower;
        if(in.displayBusy)idleAt=in.now;
        Decision out;
        if(in.screen) {
            if(sleepAllowed && (in.hints&LowLoad) && !in.displayBusy && c.quietSeconds &&
               uint32_t(in.now-idleAt)>=uint32_t(c.quietSeconds)*1000) {
                out.stage=Stage::Quiet;
                out.radiosOff=c.longest && !in.sleepBusy && !(c.protectLinks && in.connected);
            }
            out.black=screenAllowed && !(in.hints&StayLit) && !in.displayBusy && c.screenSeconds &&
                      uint32_t(in.now-idleAt)>=uint32_t(c.screenSeconds)*1000;
            return out;
        }
        out.stage=Stage::Dark;
        // Explicit protections take priority. Releasing one starts a fresh wait;
        // background work, hints and non-waking input cannot extend that wait.
        if(!timedSleep)return out;
        if(c.standbySeconds && uint32_t(in.now-darkAt)>=uint32_t(c.standbySeconds)*1000)
            out.stage=Stage::Standby;
        out.deep=timedDeep && uint32_t(in.now-deepAt)>=uint32_t(c.deepSeconds)*1000;
        return out;
    }
    constexpr void reset() {initialized=false;}
};
}
