#include "clock_discipline.hpp"
#include "clock_drift_model.hpp"
#include "settings_store.hpp"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_private/esp_clk.h"
#include "soc/rtc.h"
#include <cstddef>
#include <sys/time.h>

namespace {
using namespace sys::clock_drift;
constexpr uint32_t Magic=0x43445232U;
struct Retained {
    Window window;
    uint64_t sleepTicks=0;
    uint32_t calibration=0, spreadPpm=0, pending=0, reliable=0, magic=0, checksum=0;
};
RTC_DATA_ATTR Retained retained;
DRAM_ATTR bool calibrationArmed=false;
DRAM_ATTR uint32_t fallbackCalibration=0;
Model model;
// Only TCP/IP writes the capture; worker reads after the synchronous deinit
// fence. No NVS, mutex, UI, or allocation in the network callback.
struct Capture {int64_t beforeUs=0,networkUs=0;uint32_t count=0;bool applied=false;} capture;
uint32_t IRAM_ATTR retainedHash() {
    auto bytes=reinterpret_cast<const uint8_t *>(&retained);
    uint32_t hash=2166136261U;
    for(size_t i=0;i<offsetof(Retained,checksum);++i)hash=(hash^bytes[i])*16777619U;
    return hash;
}
void IRAM_ATTR seal() {retained.magic=Magic;retained.checksum=retainedHash();}
int64_t wallUs() {timeval tv{};gettimeofday(&tv,nullptr);return int64_t(tv.tv_sec)*Second+tv.tv_usec;}
bool setWall(int64_t us) {
    if(!epochValid(us))return false;
    timeval tv{static_cast<time_t>(us/Second),static_cast<suseconds_t>(us%Second)};
    return settimeofday(&tv,nullptr)==0;
}
bool IRAM_ATTR plausibleCalibration(uint32_t value) {
    // RC_FAST/256 is approximately 68 kHz; generous bounds detect zero/corruption.
    return value>=(8U<<19) && value<=(30U<<19);
}
}

extern "C" uint32_t __real_rtc_clk_cal(soc_clk_freq_calculation_src_t,uint32_t);
extern "C" uint32_t IRAM_ATTR __wrap_rtc_clk_cal(soc_clk_freq_calculation_src_t source,uint32_t cycles) {
    if(!calibrationArmed || source!=CLK_CAL_RTC_SLOW)
        return __real_rtc_clk_cal(source,cycles);
    calibrationArmed=false;
    // This runs in IDF's final deep-sleep calibration critical section, before
    // cache suspension. Return the longer measurement to IDF itself so its
    // short default measurement cannot overwrite our result afterward.
    uint32_t values[3]{};unsigned count=0;
    for(unsigned i=0;i<3;++i) {
        const uint32_t value=__real_rtc_clk_cal(source,3000);
        if(plausibleCalibration(value))values[count++]=value;
    }
    for(unsigned i=1;i<count;++i)for(unsigned j=i;j && values[j]<values[j-1];--j) {
        const uint32_t value=values[j];values[j]=values[j-1];values[j-1]=value;
    }
    uint32_t selected=count?values[count/2]:fallbackCalibration;
    if(!plausibleCalibration(selected))selected=__real_rtc_clk_cal(source,cycles);
    retained.spreadPpm=count>1 ? static_cast<uint32_t>(uint64_t(values[count-1]-values[0])*1000000ULL/selected) : 0;
    retained.reliable=count==3 && retained.spreadPpm<=2000 && plausibleCalibration(selected);
    retained.calibration=selected;
    retained.sleepTicks=rtc_time_get();
    retained.pending=plausibleCalibration(selected);
    seal();
    return selected;
}

extern "C" void __real_sntp_set_system_time(uint32_t,uint32_t);
extern "C" void __wrap_sntp_set_system_time(uint32_t sec,uint32_t us) {
    const int64_t before=wallUs(),network=int64_t(sec)*Second+us;
    // Preserve IDF's settimeofday, completion status, semaphore and event callback.
    __real_sntp_set_system_time(sec,us);
    const int64_t after=wallUs();
    capture.beforeUs=before;capture.networkUs=network;++capture.count;
    capture.applied=us<Second && epochValid(network) && after>=network && after-network<Second;
}

namespace sys {
void ClockDiscipline::boot() {
    model=Settings::clockDrift();
    const bool validSleep=esp_reset_reason()==ESP_RST_DEEPSLEEP &&
        esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0 &&
        retained.magic==Magic && retained.checksum==retainedHash() && retained.pending;
    if(!validSleep) {retained={};seal();return;}
    const uint64_t ticks=rtc_time_get();
    retained.pending=0; // consume once; rejected wake-stub clicks never run boot()
    const uint64_t delta=ticks>=retained.sleepTicks ? ticks-retained.sleepTicks : UINT64_MAX;
    // Bound before multiplying a corrupt/rolled-over tick delta by calibration.
    const int64_t raw=delta<=uint64_t(MaxInterval/8) && plausibleCalibration(retained.calibration) ?
        static_cast<int64_t>(rtc_time_slowclk_to_us(delta,retained.calibration)) : 0;
    int64_t applied=0;
    if(raw>0 && raw<=MaxInterval && retained.reliable && epochValid(wallUs())) {
        applied=correction(raw,model);
        if(applied && !setWall(wallUs()+applied)) {retained.window={};applied=0;}
        else addSleep(retained.window,raw,applied);
    } else retained.window={};
    ESP_LOGI("clock","wake raw=%lldms correction=%lldus rate=%ldppb samples=%lu cal=%lu spread=%luppm valid=%lu",
        static_cast<long long>(raw/1000),static_cast<long long>(applied),static_cast<long>(model.ratePpb),
        static_cast<unsigned long>(model.samples),static_cast<unsigned long>(retained.calibration),
        static_cast<unsigned long>(retained.spreadPpm),static_cast<unsigned long>(retained.reliable));
    seal();
}
void ClockDiscipline::prepareSleep() {
    // No log/lock/allocation in the final calibration wrapper. This point still
    // has a normal task context and all SNTP activity has already been stopped.
    fallbackCalibration=esp_clk_slowclk_cal_get();
    retained.pending=0;seal();calibrationArmed=true;
}
void ClockDiscipline::networkStopped() {
    if(!capture.count)return;
    const auto sample=capture;capture={};
    const auto previous=retained.window;
    Result result=Result::Invalid;
    if(sample.count==1 && sample.applied)
        result=synchronize(model,retained.window,sample.beforeUs,sample.networkUs);
    else retained.window={};
    if(result==Result::Learned)Settings::saveClockDrift(model);
    ESP_LOGI("clock","NTP offset=%lldus sleep=%lldms prior_correction=%lldus rate=%ldppb samples=%lu result=%u",
        static_cast<long long>(sample.networkUs-sample.beforeUs),static_cast<long long>(previous.sleepUs/1000),
        static_cast<long long>(previous.appliedUs),static_cast<long>(model.ratePpb),
        static_cast<unsigned long>(model.samples),static_cast<unsigned>(result));
    seal();
}
void ClockDiscipline::manualTimeChanged() {retained.window={};seal();}
}
