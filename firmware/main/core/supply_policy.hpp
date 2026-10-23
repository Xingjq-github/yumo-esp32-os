#pragma once
#include <cstdint>

namespace sys::supply {
// ETA6098 STAT high cannot distinguish charge completion from absent VBUS.
// USB SOF proves a powered host, but no SOF does not prove cable removal.
// Inferred battery is a power-policy fallback, not proof that VBUS is absent.
enum class State : uint8_t { Unknown, External, BatteryConfirmed, BatteryInferred, Protected };
constexpr int64_t kFreshMs=3000,kAbsentMs=2000,kInferMs=30000;
class Monitor {
    State state=State::Unknown;
    int64_t at=-1,absentAt=-1;
    bool manualProtection=false;
public:
    constexpr void clearConfirmation() {state=State::Protected;manualProtection=true;}
    constexpr bool canConfirm(int64_t now) const {
        const auto value=snapshot(now);
        return (value==State::Unknown || value==State::Protected) && absentAt>=0 && now-absentAt>=kAbsentMs;
    }
    constexpr State observe(bool signal,int64_t now,bool confirmBattery=false) {
        if(at<0 || now-at>kFreshMs) {state=State::Unknown;absentAt=-1;}
        at=now;
        if(signal) {state=State::External;absentAt=-1;manualProtection=false;}
        else {
            if(absentAt<0)absentAt=now;
            if(state==State::External)state=State::Unknown;
            if(confirmBattery && now-absentAt>=kAbsentMs) {
                state=State::BatteryConfirmed;manualProtection=false;
            } else if(manualProtection)state=State::Protected;
            else if(state!=State::BatteryConfirmed && now-absentAt>=kInferMs)
                state=State::BatteryInferred;
        }
        return state;
    }
    constexpr State snapshot(int64_t now) const {
        return at>=0 && now-at<=kFreshMs?state:State::Unknown;
    }
};
constexpr bool protects(State s) {return s!=State::BatteryConfirmed && s!=State::BatteryInferred;}
}
