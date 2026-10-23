#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace sys {
// Voltage-only estimate, NOT a coulomb counter. CHG high is not USB-present or
// charge-complete. Unknown cell chemistry/load/temperature prevent exact SOC.
struct BatterySnapshot {
    int millivolts=0, percent=-1;
    bool valid=false, charging=false, chargingKnown=false;
    int64_t sampledAtMs=-1, chargeAtMs=-1;
};
namespace battery {
constexpr int kMinMillivolts=2600, kMaxMillivolts=4450;
constexpr int64_t kStaleMs=60000, kChargeStaleMs=5000, kChargeDebounceMs=1500;
constexpr int64_t kFilterMs=20000, kSettleMs=15000, kFullHoldMs=120000;
constexpr int64_t kFallMs=15000, kRiseChargeMs=30000, kRiseRestMs=120000;
// ETA6098 4.2V variant permits 4.16V at charge termination. This is still
// an estimate, never a hardware FULL/USB-present indication.
constexpr int kFullMillivolts=4160,kBootFullMillivolts=4180;
constexpr int64_t kSmallChangeHoldMs=120000;
struct Point { int millivolts, percent; };
// Initial reference for a single 4.2 V Li-ion cell. Tune against this board's
// actual cell discharge measurements, not nominal mAh or an invented capacity.
constexpr Point kCurve[]={{3300,0},{3500,5},{3600,10},{3700,20},{3750,35},
    {3800,50},{3900,65},{4000,80},{4100,90},{4200,100}};
constexpr int percentForVoltage(int mv) {
    if(mv<=kCurve[0].millivolts) return 0;
    for(size_t i=1;i<sizeof(kCurve)/sizeof(kCurve[0]);++i) {
        const auto &a=kCurve[i-1], &b=kCurve[i];
        if(mv<=b.millivolts) {
            const int span=b.millivolts-a.millivolts;
            return a.percent+((mv-a.millivolts)*(b.percent-a.percent)+span/2)/span;
        }
    }
    return 100;
}
// Allocation-free worker-owned estimator. Publish only a copied snapshot.
class Estimator {
public:
    constexpr void observeCharge(bool active,int64_t now) {
        if(chargeAt<0 || now-chargeAt>kChargeStaleMs) { candidateAt=-1; chargeKnown=false; }
        chargeAt=now;
        if(candidateAt<0 || active!=candidate) { candidate=active; candidateAt=now; }
        if(now-candidateAt>=kChargeDebounceMs && (!chargeKnown || charging!=candidate)) {
            chargeKnown=true; charging=candidate;
            settleUntil=now+kSettleMs; trend=0; trendAt=now;
            fullAt=-1; // charging voltage is not a full-charge indication
        }
    }
    constexpr void observeVoltage(int mv,int64_t now) {
        if(mv<kMinMillivolts || mv>kMaxMillivolts) return;
        const bool seed=sampleAt<0 || now-sampleAt>kStaleMs;
        if(seed) { filtered=static_cast<float>(mv); fullAt=-1;chargedHigh=false;smallAt=-1; }
        else {
            const auto dt=std::clamp<int64_t>(now-sampleAt,0,30000);
            filtered+=(mv-filtered)*(static_cast<float>(dt)/(kFilterMs+dt));
        }
        sampleAt=now;
        const int voltage=static_cast<int>(filtered+0.5f);
        if(voltage<4100) chargedHigh=false;
        const bool known=chargeKnown && now-chargeAt<=kChargeStaleMs;
        if(known && charging && voltage>=kFullMillivolts) chargedHigh=true;
        const int fullThreshold=chargedHigh?kFullMillivolts:kBootFullMillivolts;
        if(known && !charging && voltage>=fullThreshold) {
            if(fullAt<0) fullAt=now;
        } else fullAt=-1;
        int target=percentForVoltage(voltage);
        // A qualified near-full plateau must actually reach 100%, not remain
        // blocked by a separate curve requiring 4.195V.
        if(fullAt>=0 && now-fullAt>=kFullHoldMs) target=100;
        // Do not jump to 100% on insertion of a charger. No claim of 'full'.
        if(!known || charging || fullAt<0 || now-fullAt<kFullHoldMs) target=std::min(target,99);
        if(seed) { percent=target; trend=0; trendAt=now; return; }
        if(known && charging) percent=std::min(percent,99);
        if(now<settleUntil) { trend=0; trendAt=now; return; }
        const int difference=target-percent;
        // 2-point hysteresis, except endpoints so 0/100 remain reachable.
        const bool endpoint=(target==0 || target==100) && difference!=0;
        if(difference==0) { trend=0;trendAt=now;smallAt=-1;return; }
        if(!endpoint && difference>-2 && difference<2) {
            if(smallAt<0 || smallTarget!=target) { smallTarget=target;smallAt=now; }
            if(now-smallAt<kSmallChangeHoldMs) { trend=0;trendAt=now;return; }
        } else smallAt=-1;
        const int direction=difference>0?1:-1;
        if(direction!=trend) { trend=direction; trendAt=now; return; }
        const auto interval=direction<0?kFallMs:(known && charging?kRiseChargeMs:kRiseRestMs);
        if(now-trendAt<interval) return;
        // Downward catch-up is bounded but faster than upward recovery; a screen
        // load change must not make the battery appear to 'recharge' in seconds.
        percent+=direction>0?1:std::max(difference,-3);
        percent=std::clamp(percent,0,100); trendAt=now;
    }
    constexpr BatterySnapshot snapshot(int64_t now) const {
        BatterySnapshot out;
        out.sampledAtMs=sampleAt; out.chargeAtMs=chargeAt;
        out.valid=sampleAt>=0 && now-sampleAt<=kStaleMs;
        out.chargingKnown=chargeKnown && chargeAt>=0 && now-chargeAt<=kChargeStaleMs;
        out.charging=out.chargingKnown && charging;
        if(out.valid) { out.millivolts=static_cast<int>(filtered+0.5f); out.percent=percent; }
        return out;
    }
private:
    float filtered=0;
    int percent=-1, trend=0;
    bool charging=false, chargeKnown=false, candidate=false,chargedHigh=false;
    int smallTarget=-1;int64_t smallAt=-1;
    int64_t sampleAt=-1, chargeAt=-1, candidateAt=-1, settleUntil=0, trendAt=0, fullAt=-1;
};
} // namespace battery
} // namespace sys
