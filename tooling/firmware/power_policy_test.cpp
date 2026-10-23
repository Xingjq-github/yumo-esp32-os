#include "../../firmware/main/core/power_policy.hpp"
using namespace sys::power;
constexpr bool normalTimeline() {
    Policy p;Config c;Input i{};p.step(c,i);
    i.now=59999;if(p.step(c,i).black)return false;
    i.now=60000;if(!p.step(c,i).black)return false;
    i.screen=false;p.step(c,i);
    i.now=179999;if(p.step(c,i).stage!=Stage::Dark)return false;
    i.now=180000;if(p.step(c,i).stage!=Stage::Standby)return false;
    i.now=299999;if(p.step(c,i).deep)return false;
    i.now=300000;return p.step(c,i).deep;
}
constexpr bool keepLit() {
    Policy p;Config c;Input i{};i.hints=StayLit|LowLoad|Dim;p.step(c,i);
    i.now=60000;auto d=p.step(c,i);if(d.black||d.deep||d.stage!=Stage::Quiet||!d.radiosOff)return false;
    i.now=3600000;d=p.step(c,i);if(d.black||d.deep||d.stage!=Stage::Quiet)return false;
    ++i.activity;if(p.step(c,i).stage!=Stage::Active)return false;
    i.now+=60000;c.longest=0;d=p.step(c,i);return d.stage==Stage::Quiet&&!d.radiosOff;
}
constexpr bool busyProtection() {
    Policy p;Config c;Input i{};i.screen=false;p.step(c,i);
    i.now=120000;i.sleepBusy=true;if(p.step(c,i).stage!=Stage::Standby)return false;
    i.now=239999;if(p.step(c,i).deep)return false;
    i.now=240000;if(!p.step(c,i).deep)return false;
    i.hints=KeepSession;i.now=1000000;return p.step(c,i).deep;
}
constexpr bool linksAndOff() {
    Policy p;Config c;Input i{};i.screen=false;i.connected=true;c.protectLinks=1;p.step(c,i);
    i.now=300000;if(p.step(c,i).deep || p.step(c,i).stage!=Stage::Standby)return false;
    c.protectLinks=0;if(p.step(c,i).deep)return false;
    i.now=539999;if(p.step(c,i).deep)return false;
    i.now=540000;if(!p.step(c,i).deep)return false;
    c.automatic=0;i.now=900000;const auto d=p.step(c,i);return !d.deep&&d.stage==Stage::Dark;
}
constexpr bool rollover() {
    Policy p;Config c;Input i{};i.now=0xffff0000;p.step(c,i);
    i.now+=59000;if(p.step(c,i).black)return false;
    i.now+=1000;if(!p.step(c,i).black)return false;
    ++i.activity;i.now+=20000;if(p.step(c,i).black)return false;
    i.now+=60000;return p.step(c,i).black;
}
constexpr bool transactionAndNavigation() {
    Policy p;Config c;Input i{};p.step(c,i);
    i.displayBusy=true;i.now=90000;if(p.step(c,i).black)return false;
    i.displayBusy=false;i.now=90001;if(p.step(c,i).black)return false;
    i.now=149999;if(p.step(c,i).black)return false;
    i.now=150000;if(!p.step(c,i).black)return false;
    i.hints=StayLit;i.now=150001;if(p.step(c,i).black)return false;
    i.hints=None;i.now=150002;if(p.step(c,i).black)return false;
    i.now+=60000;return p.step(c,i).black;
}
static_assert(normalTimeline());static_assert(keepLit());static_assert(busyProtection());
static_assert(linksAndOff());static_assert(rollover());static_assert(transactionAndNavigation());
static_assert(decode(encode(Config{})).deepSeconds==240);
static_assert(decode({9,0,0,0,0,0,0,0,0}).screenSeconds==60);
static_assert(normalize({60,60,240,120,1,2,2,2}).deepSeconds==250);

constexpr bool pluggedCombinations() {
    for(unsigned sleep=0;sleep<2;++sleep)for(unsigned black=0;black<2;++black) {
        Config c;c.sleepOnPower=sleep;c.screenOffOnPower=black;
        Input i;i.supplyProtected=true;Policy p;p.step(c,i);
        i.now=60000;if(p.step(c,i).black!=bool(black))return false;
        i.hints=StayLit|LowLoad|Dim;p.step(c,i);i.now+=60000;
        const auto lit=p.step(c,i);
        if(lit.black || lit.deep || (lit.stage==Stage::Quiet)!=bool(sleep) || lit.radiosOff!=bool(sleep))return false;
        i.screen=false;p.step(c,i);i.now+=120000;
        if((p.step(c,i).stage==Stage::Standby)!=bool(sleep))return false;
        i.now+=120000;if(p.step(c,i).deep!=bool(sleep))return false;
    }
    return true;
}
constexpr bool unplugRestartsTimers() {
    Config c;Input i;i.supplyProtected=true;Policy p;p.step(c,i);
    i.now=3600000;if(p.step(c,i).black)return false;
    i.supplyProtected=false;if(p.step(c,i).black)return false;
    i.now+=59999;if(p.step(c,i).black)return false;
    ++i.now;if(!p.step(c,i).black)return false;
    i.screen=false;p.step(c,i);i.now+=239999;
    i.supplyProtected=true;if(p.step(c,i).deep)return false;
    ++i.now;if(p.step(c,i).deep)return false;
    i.now+=3600000;if(p.step(c,i).deep)return false;
    i.supplyProtected=false;if(p.step(c,i).deep)return false;
    i.now+=119999;if(p.step(c,i).stage!=Stage::Dark)return false;
    ++i.now;if(p.step(c,i).stage!=Stage::Standby)return false;
    i.now+=120000;return p.step(c,i).deep;
}
constexpr bool uninterruptedDarkness() {
    // With explicit permission, background changes must not extend deadlines.
    for(unsigned mask=0;mask<64;++mask) {
        Config c;c.sleepOnPower=1;c.protectLinks=0;Policy p;Input i;i.screen=false;
        i.now=0xffff0000;p.step(c,i);
        i.now+=119999;i.sleepBusy=mask&1;i.displayBusy=mask&2;i.connected=mask&4;
        i.supplyProtected=mask&8;i.hints=(mask&16)?StayLit|LowLoad|KeepSession:None;
        i.activity+=(mask&32)?1:0;
        if(p.step(c,i).stage!=Stage::Dark)return false;
        ++i.now;if(p.step(c,i).stage!=Stage::Standby)return false;
        i.now+=119999;if(p.step(c,i).deep)return false;
        ++i.now;if(!p.step(c,i).deep)return false;
        i.screen=true;if(p.step(c,i).deep)return false;
        i.screen=false;if(p.step(c,i).deep)return false;
        i.now+=240000;if(!p.step(c,i).deep)return false;
    }
    Config c;Policy p;Input i;i.screen=false;p.step(c,i);
    c.automatic=0;i.now=240000;if(p.step(c,i).deep)return false;
    c.automatic=1;if(p.step(c,i).deep)return false;
    i.now+=240000;if(!p.step(c,i).deep)return false;
    c.deepSeconds=0;if(p.step(c,i).deep)return false;
    c.deepSeconds=240;if(p.step(c,i).deep)return false;
    i.now+=240000;return p.step(c,i).deep;
}
static_assert(uninterruptedDarkness());
constexpr bool migratePowerSettings() {
    constexpr auto c=decodeLegacy({1,300,120,180,600,35,1,0,1});
    if(c.screenSeconds!=300 || c.quietSeconds!=120 || c.standbySeconds!=180 || c.deepSeconds!=600 ||
       c.brightness!=35 || c.automatic!=1 || c.longest || c.protectLinks!=1 || c.sleepOnPower || c.screenOffOnPower)return false;
    auto adjusted=c;adjusted.sleepOnPower=1;adjusted.screenOffOnPower=1;
    const auto restored=decode(encode(adjusted));
    return restored.sleepOnPower && restored.screenOffOnPower && restored.screenSeconds==300 && restored.protectLinks;
}
static_assert(pluggedCombinations());static_assert(unplugRestartsTimers());static_assert(migratePowerSettings());
