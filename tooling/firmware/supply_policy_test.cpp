#include "../../firmware/main/core/supply_policy.hpp"
using namespace sys::supply;
constexpr bool supplyTransitions() {
    Monitor m;
    if(!protects(m.snapshot(0)))return false;
    if(m.observe(false,0)!=State::Unknown)return false;
    if(m.observe(false,1999,true)!=State::Unknown)return false;
    if(m.observe(false,2000,true)!=State::BatteryConfirmed)return false;
    if(m.observe(false,3000)!=State::BatteryConfirmed)return false;
    if(m.observe(true,3100)!=State::External)return false;
    if(m.observe(true,4000,true)!=State::External)return false; // cannot override live evidence
    if(m.observe(false,5000)!=State::Unknown)return false; // full charger OR suspended PC OR unplug
    for(int t=6000;t<=20000;t+=1000)if(m.observe(false,t)!=State::Unknown)return false;
    if(m.observe(false,21000,true)!=State::BatteryConfirmed)return false;
    if(m.snapshot(24000)!=State::BatteryConfirmed)return false;
    if(m.snapshot(24001)!=State::Unknown)return false; // failed polling is never "unplugged"
    if(m.observe(false,25000)!=State::Unknown)return false;
    if(m.observe(true,25001)!=State::External)return false;
    m.observe(false,26000);m.observe(false,28000,true);
    m.clearConfirmation();return m.observe(false,29000)==State::Protected && m.canConfirm(29000);
}
static_assert(supplyTransitions());

constexpr bool automaticBatteryFallback() {
    Monitor m;
    for(int t=0;t<30000;t+=1000)if(m.observe(false,t)!=State::Unknown)return false;
    if(m.observe(false,30000)!=State::BatteryInferred || protects(m.snapshot(30000)))return false;
    // A whole unplugged night does not need a manual battery confirmation.
    for(int t=31000;t<=28800000;t+=1000)if(m.observe(false,t)!=State::BatteryInferred)return false;
    if(m.observe(true,28800001)!=State::External)return false;
    for(int t=28801000;t<28831000;t+=1000)if(m.observe(false,t)!=State::Unknown)return false;
    if(m.observe(false,28831000)!=State::BatteryInferred)return false;
    // A missed observation restarts the full absence window, never guesses from stale input.
    if(m.snapshot(28834001)!=State::Unknown || m.observe(false,28835000)!=State::Unknown)return false;
    for(int t=28836000;t<28865000;t+=1000)if(m.observe(false,t)!=State::Unknown)return false;
    return m.observe(false,28865000)==State::BatteryInferred;
}
constexpr bool manualProtectionSurvivesFallback() {
    Monitor m;m.observe(false,0);
    for(int t=1000;t<=30000;t+=1000)m.observe(false,t);
    m.clearConfirmation();
    for(int t=31000;t<=300000;t+=1000)if(m.observe(false,t)!=State::Protected)return false;
    if(m.observe(false,305000)!=State::Protected)return false; // sampling gaps cannot revoke explicit protection
    if(m.observe(false,307000,true)!=State::BatteryConfirmed)return false;
    m.clearConfirmation();
    if(m.observe(true,307001,true)!=State::External)return false; // real signal always wins
    for(int t=308000;t<338000;t+=1000)if(m.observe(false,t)!=State::Unknown)return false;
    return m.observe(false,338000)==State::BatteryInferred;
}
static_assert(automaticBatteryFallback());
static_assert(manualProtectionSurvivesFallback());
