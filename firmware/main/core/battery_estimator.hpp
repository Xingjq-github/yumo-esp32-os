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
constexpr int64_t kDisplayFilterMs=30000;
// Rates are time per percentage point. Fractional progress is retained so a
// slow discharge does not accumulate a debt and then drop three points at once.
constexpr int64_t kFallMs=5000, kRiseChargeMs=30000, kRiseRestMs=120000;
constexpr int kLevelScale=1000, kDisplayDeadband=150;
// User-selected display calibration: the old nominal 91% at 4.110V is
// now 100%. This is NOT a charger setpoint or proof of physical full charge.
// Use the same display endpoint after boot and after observing charging.
constexpr int kFullMillivolts=4110;
struct Point { int millivolts, percent; };
// Conservative voltage-only fallback, pending a measured discharge trace for
// this cell. Do not rescale all old percentages by 100/91: that overstated the
// middle of the discharge (3.80V = 55%) and crowded 33 points into 100mV.
// Spread that transition over the upper/middle range, retaining the selected
// display endpoints. These are estimates, not measured capacity or runtime.
constexpr Point kCurve[]={{3300,0},{3500,5},{3600,10},{3700,18},{3750,28},
    {3800,40},{3850,52},{3900,64},{3950,75},{4000,85},{4050,93},{4110,100}};
static_assert(kCurve[sizeof(kCurve)/sizeof(kCurve[0])-1].millivolts==kFullMillivolts,
    "Display curve and full qualification must share the same endpoint");
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
            settleUntil=now+kSettleMs;
            fullAt=-1; // charging voltage is not a full-charge indication
        }
    }
    constexpr void observeVoltage(int mv,int64_t now) {
        if(mv<kMinMillivolts || mv>kMaxMillivolts || now<=sampleAt) return;
        const bool seed=sampleAt<0 || now-sampleAt>kStaleMs;
        if(seed) {
            filtered=static_cast<float>(mv); fullAt=-1;
            recent[0]=recent[1]=recent[2]=mv; recentIndex=0;
        }
        else {
            // Reject one isolated load/ADC excursion across batches. Sustained
            // changes pass on the next sample (5s lit / 15s dark), not minutes.
            recent[recentIndex]=mv; recentIndex=(recentIndex+1)%3;
            const int a=recent[0], b=recent[1], c=recent[2];
            const int median=a+b+c-std::min(a,std::min(b,c))-std::max(a,std::max(b,c));
            const auto dt=std::clamp<int64_t>(now-sampleAt,0,30000);
            filtered+=(median-filtered)*(static_cast<float>(dt)/(kFilterMs+dt));
        }
        sampleAt=now;
        const int voltage=static_cast<int>(filtered+0.5f);
        const bool known=chargeKnown && now-chargeAt<=kChargeStaleMs;
        if(known && !charging && voltage>=kFullMillivolts) {
            if(fullAt<0) fullAt=now;
        } else fullAt=-1;
        if(seed) {
            percent=targetPercent(now); displayed=percent*kLevelScale;
            updatedAt=now; return;
        }
        advance(now);
    }
    // Called by the existing worker publication tick, including between ADC
    // batches. A 15s dark-screen sample must not cause a three-point UI jump.
    // This does no I/O and never advances an expired sample.
    constexpr void advance(int64_t now) {
        if(sampleAt<0 || now-sampleAt>kStaleMs || now<=updatedAt) return;
        const auto dt=std::clamp<int64_t>(now-updatedAt,0,30000);
        updatedAt=now;
        const bool known=chargeKnown && now-chargeAt<=kChargeStaleMs;
        if(known && charging) {
            displayed=std::min(displayed,99.0f*kLevelScale);
            percent=std::min(percent,99);
        }
        if(now<settleUntil) return;
        const int target=targetPercent(now);
        const float difference=target*kLevelScale-displayed;
        const auto riseMs=known && charging?kRiseChargeMs:kRiseRestMs;
        const float step=difference*(static_cast<float>(dt)/(kDisplayFilterMs+dt));
        const float fallLimit=static_cast<float>(kLevelScale*dt)/kFallMs;
        const float riseLimit=static_cast<float>(kLevelScale*dt)/riseMs;
        displayed=std::clamp(displayed+std::clamp(step,-fallLimit,riseLimit),
                             0.0f,100.0f*kLevelScale);
        const int rounded=static_cast<int>(displayed/kLevelScale+0.5f);
        // A small rounding deadband suppresses boundary flicker without a
        // two-minute hold. Even a sustained one-point target change converges.
        const float threshold=kLevelScale/2+kDisplayDeadband;
        if((rounded>percent && displayed-percent*kLevelScale>=threshold) ||
           (rounded<percent && percent*kLevelScale-displayed>=threshold) ||
           (rounded==target && (target==0 || target==100))) percent=rounded;
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
    constexpr int targetPercent(int64_t now) const {
        const bool known=chargeKnown && now-chargeAt<=kChargeStaleMs;
        int target=percentForVoltage(static_cast<int>(filtered+0.5f));
        // Qualify the user-selected display endpoint; never infer charge
        // completion or USB presence from this displayed percentage.
        if(fullAt>=0 && now-fullAt>=kFullHoldMs) target=100;
        // Do not jump to 100% on insertion of a charger. No claim of 'full'.
        if(!known || charging || fullAt<0 || now-fullAt<kFullHoldMs) target=std::min(target,99);
        return target;
    }
    float filtered=0, displayed=0;
    int percent=-1;
    int recent[3]{}, recentIndex=0;
    bool charging=false, chargeKnown=false, candidate=false;
    int64_t sampleAt=-1, chargeAt=-1, candidateAt=-1, settleUntil=0, updatedAt=-1, fullAt=-1;
};
} // namespace battery
} // namespace sys
