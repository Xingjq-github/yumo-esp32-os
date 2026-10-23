#include "core/settings_store.hpp"
#include "esp_system.h"
#include "esp_sleep.h"
#include "soc/rtc.h"
#include "sys/time.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace sys::clock_drift;
static int64_t fakeWall=1800000000LL*Second;
static uint64_t fakeTicks=1000000;
static int resetReason=0,wakeCause=0,sets=0,notifications=0,stores=0;
static bool setFails=false,inCallback=false;
static uint32_t calDefault=15U<<19;
static std::vector<uint32_t> calValues,calCycles;
static sys::clock_drift::Model stored;
int gettimeofday(timeval *v,void*){v->tv_sec=fakeWall/Second;v->tv_usec=fakeWall%Second;return 0;}
int settimeofday(const timeval *v,const void*){++sets;if(setFails)return -1;fakeWall=int64_t(v->tv_sec)*Second+v->tv_usec;return 0;}
int esp_reset_reason(){return resetReason;}int esp_sleep_get_wakeup_cause(){return wakeCause;}
uint32_t esp_clk_slowclk_cal_get(){return calDefault;}
uint64_t rtc_time_get(){return fakeTicks;}
uint64_t rtc_time_slowclk_to_us(uint64_t ticks,uint32_t cal){return (ticks*cal)>>19;}
sys::clock_drift::Model sys::Settings::clockDrift(){return stored;}
void sys::Settings::saveClockDrift(sys::clock_drift::Model value){CHECK(!inCallback);stored=value;++stores;}
extern "C" uint32_t __real_rtc_clk_cal(soc_clk_freq_calculation_src_t,uint32_t cycles) {
    calCycles.push_back(cycles);
    if(calValues.empty())return calDefault;
    const auto value=calValues.front();calValues.erase(calValues.begin());return value;
}
extern "C" void __real_sntp_set_system_time(uint32_t s,uint32_t u) {
    timeval tv{static_cast<time_t>(s),static_cast<suseconds_t>(u)};
    settimeofday(&tv,nullptr);++notifications;
}
#include "core/clock_discipline.cpp"
using sys::ClockDiscipline;
static void ntp(int64_t us) {
    inCallback=true;const auto oldStores=stores;
    __wrap_sntp_set_system_time(us/Second,us%Second);
    CHECK(stores==oldStores);inCallback=false;ClockDiscipline::networkStopped();
}
static void sleep(int64_t rawUs) {
    calCycles.clear();ClockDiscipline::prepareSleep();
    const auto cal=__wrap_rtc_clk_cal(CLK_CAL_RTC_SLOW,10);
    CHECK(calCycles==std::vector<uint32_t>({3000,3000,3000}));
    CHECK(retained.pending&&retained.checksum==retainedHash());
    fakeTicks+=rawUs/15;fakeWall+=rawUs;
    resetReason=ESP_RST_DEEPSLEEP;wakeCause=ESP_SLEEP_WAKEUP_EXT0;
    CHECK(cal==calDefault);ClockDiscipline::boot();
}
int main() {
    ClockDiscipline::boot();CHECK(!retained.window.anchorUs&&!retained.pending);
    CHECK(__wrap_rtc_clk_cal(CLK_CAL_RTC_SLOW,10)==calDefault);
    CHECK(calCycles==std::vector<uint32_t>({10}));calCycles.clear();
    const auto epoch=fakeWall;ntp(epoch);CHECK(notifications==1&&retained.window.anchorUs==epoch);
    sleep(16*Hour+240*Second);CHECK(stores==0);ntp(epoch+16*Hour);
    CHECK(stores==1&&stored.samples==1&&stored.ratePpb<0);
    const auto learned=stored.ratePpb;
    // The model survives a deep boot, and already applied corrections are learned correctly.
    for(int i=0;i<20;++i) {
        const auto start=fakeWall;sleep(16*Hour+240*Second);
        CHECK(std::llabs(fakeWall-start-16*Hour)<100);
        ntp(start+16*Hour);CHECK(std::abs(stored.ratePpb-learned)<=2);
    }
    const auto old=fakeWall;const auto oldSets=sets;
    ClockDiscipline::boot();CHECK(fakeWall==old&&sets==oldSets); // no repeated correction
    CHECK(retained.window.anchorUs==0); // unexpected second boot discards the window
    ntp(fakeWall);ClockDiscipline::manualTimeChanged();CHECK(!retained.window.anchorUs);
    CHECK(stored.ratePpb==learned);
    // Cold resets never interpret stale RTC bytes as a slept interval.
    ntp(fakeWall);ClockDiscipline::prepareSleep();__wrap_rtc_clk_cal(CLK_CAL_RTC_SLOW,10);
    resetReason=0;ClockDiscipline::boot();CHECK(!retained.window.anchorUs&&!retained.pending);
    CHECK(stored.ratePpb==learned);
    // Invalid calibration and a corrupt RTC checksum cannot apply a correction.
    ntp(fakeWall);calValues={0,0,0};sleep(Hour);CHECK(!retained.window.anchorUs);
    ClockDiscipline::prepareSleep();__wrap_rtc_clk_cal(CLK_CAL_RTC_SLOW,10);
    retained.sleepTicks^=1;ClockDiscipline::boot();CHECK(!retained.pending&&!retained.window.anchorUs);
    // Unstable long samples remain diagnostic-only for learning/correction.
    ntp(fakeWall);calValues={calDefault,calDefault+100000,calDefault};sleep(Hour);
    CHECK(!retained.window.anchorUs&&!retained.reliable);
    // Failed settimeofday invalidates observation, rather than counting unapplied correction.
    ntp(fakeWall);setFails=true;sleep(Hour);CHECK(!retained.window.anchorUs);setFails=false;
    // SNTP capture sees the clock BEFORE the step and preserves SDK notification.
    const auto before=fakeWall;inCallback=true;__wrap_sntp_set_system_time((fakeWall+123*Second)/Second,0);inCallback=false;
    CHECK(capture.beforeUs==before&&capture.networkUs==before+123*Second&&capture.applied);
    ClockDiscipline::networkStopped();CHECK(capture.count==0);
    const auto count=stores;ClockDiscipline::networkStopped();CHECK(stores==count);
    // A completed SNTP step followed by manual setting cannot retain the old anchor.
    ntp(fakeWall);ClockDiscipline::manualTimeChanged();CHECK(!retained.window.anchorUs);
    // Rejected wake-stub clicks do not touch application state. Accumulate all ticks
    // until one accepted deep wake; the pending marker is consumed exactly once.
    ntp(fakeWall);ClockDiscipline::prepareSleep();__wrap_rtc_clk_cal(CLK_CAL_RTC_SLOW,10);
    for(int i=0;i<10;++i){fakeTicks+=Hour/15;fakeWall+=Hour;CHECK(retained.pending);}
    ClockDiscipline::boot();CHECK(!retained.pending&&retained.window.sleepUs==10*Hour);
    std::printf("%u production clock adapter checks passed\n",checks);
}
