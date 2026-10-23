#include "battery_mgr.hpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "system_service.hpp"
#include "power_mgr.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <atomic>
#include <mutex>

namespace sys {
namespace {
constexpr adc_unit_t kUnit=ADC_UNIT_1;
constexpr adc_channel_t kChannel=ADC_CHANNEL_0; // GPIO1, board divider 3:1
// VBAT/3 is 0.87..1.48V: use the narrower calibrated 6dB range instead of
// 12dB. This reduces ADC input error without inventing a board voltage offset.
constexpr adc_atten_t kAtten=ADC_ATTEN_DB_6;
constexpr gpio_num_t kChargePin=GPIO_NUM_3; // Touch board BSP: active-low CHG
constexpr int kDividerRatio=3, kSamples=16, kMinimumSamples=12;
adc_oneshot_unit_handle_t s_adc=nullptr;
adc_cali_handle_t s_cali=nullptr;
std::atomic<bool> s_ready{false};
std::mutex s_snapshotMutex;
BatterySnapshot s_published;
battery::Estimator s_estimator; // boot, then one SystemService writer
std::atomic<bool> s_chargeReady{false};
bool s_wasScreenOn=true;
std::mutex s_supplyMutex; // never held across ADC, I/O, or Power calls
supply::Monitor s_supply;
int64_t s_nextSupply=0;
std::atomic<unsigned> s_supplyRequest{0}; // 1: battery confirmation, 2: restore protection
int64_t s_nextSample=0;
void publish(int64_t now) {
    s_estimator.advance(now);
    const auto value=s_estimator.snapshot(now);
    const std::lock_guard<std::mutex> lock(s_snapshotMutex);
    s_published=value;
}
} // namespace

bool Battery::init() {
    if(s_ready.load()) return true;
    // Power detection remains available even if ADC calibration fails.
    gpio_config_t charge{}; charge.pin_bit_mask=1ULL<<kChargePin;
    charge.mode=GPIO_MODE_INPUT; charge.pull_up_en=GPIO_PULLUP_ENABLE;
    charge.pull_down_en=GPIO_PULLDOWN_DISABLE; charge.intr_type=GPIO_INTR_DISABLE;
    s_chargeReady=gpio_config(&charge)==ESP_OK;
    if(!s_chargeReady) ESP_LOGW("battery","CHG input unavailable; charge state unknown");
    adc_oneshot_unit_init_cfg_t initCfg{};
    initCfg.unit_id=kUnit; initCfg.ulp_mode=ADC_ULP_MODE_DISABLE;
    if(adc_oneshot_new_unit(&initCfg,&s_adc)!=ESP_OK) return false;
    adc_oneshot_chan_cfg_t chanCfg{};
    chanCfg.bitwidth=ADC_BITWIDTH_DEFAULT; chanCfg.atten=kAtten;
    if(adc_oneshot_config_channel(s_adc,kChannel,&chanCfg)!=ESP_OK) {
        (void)adc_oneshot_del_unit(s_adc); s_adc=nullptr; return false;
    }
    adc_cali_curve_fitting_config_t caliCfg{};
    caliCfg.unit_id=kUnit; caliCfg.chan=kChannel;
    caliCfg.atten=kAtten; caliCfg.bitwidth=ADC_BITWIDTH_DEFAULT;
    if(adc_cali_create_scheme_curve_fitting(&caliCfg,&s_cali)!=ESP_OK) {
        ESP_LOGW("battery","ADC calibration unavailable; show unknown, not raw SOC");
        (void)adc_oneshot_del_unit(s_adc); s_adc=nullptr; return false;
    }
    s_ready.store(true);
    poll(true);
    return true;
}

void Battery::sample() {
    if(!s_ready.load()) return;
    int values[kSamples]{}; int count=0;
    for(int i=0;i<kSamples;++i) {
        int raw=0,mv=0;
        if(adc_oneshot_read(s_adc,kChannel,&raw)!=ESP_OK ||
           adc_cali_raw_to_voltage(s_cali,raw,&mv)!=ESP_OK) continue;
        mv*=kDividerRatio;
        if(mv>=battery::kMinMillivolts && mv<=battery::kMaxMillivolts) values[count++]=mv;
    }
    const auto now=esp_timer_get_time()/1000;
    if(count>=kMinimumSamples) {
        std::sort(values,values+count);
        const int trim=count/4; int total=0;
        for(int i=trim;i<count-trim;++i) total+=values[i];
        const int kept=count-2*trim;
        s_estimator.observeVoltage((total+kept/2)/kept,now);
    } // transient ADC failure preserves the last good sample, not a false 0%
    publish(now);
}
void Battery::pollSupply(bool force) {
    bool changed=false;
    {
        const std::lock_guard<std::mutex> lock(s_supplyMutex);
        const auto now=esp_timer_get_time()/1000;
        if(!force && !s_supplyRequest.load() && now<s_nextSupply)return;
        s_nextSupply=now+1000;
        // This short path can run during a slow service command. Never move ADC
        // sampling here: stale evidence still fails closed if BOTH tasks stall.
        const bool external=usb_serial_jtag_is_connected() || (s_chargeReady && gpio_get_level(kChargePin)==0);
        const auto previous=s_supply.snapshot(now);
        const auto request=s_supplyRequest.exchange(0);
        if(request==2)s_supply.clearConfirmation();
        const auto next=s_supply.observe(external,now,request==1);
        changed=supply::protects(previous)!=supply::protects(next);
    }
    if(changed)Power::activity();
}
void Battery::poll(bool screenOn) {
    pollSupply(true); // fresh evidence immediately before automatic sleep teardown
    const auto now=esp_timer_get_time()/1000;
    if(!s_ready.load()) return;
    if(s_chargeReady) s_estimator.observeCharge(gpio_get_level(kChargePin)==0,now);
    if(now>=s_nextSample || (screenOn && !s_wasScreenOn)) {
        s_nextSample=now+(screenOn?5000:15000); sample();
    } else publish(now);
    s_wasScreenOn=screenOn;
}
BatterySnapshot Battery::snapshot() {
    BatterySnapshot out;
    { const std::lock_guard<std::mutex> lock(s_snapshotMutex); out=s_published; }
    const auto now=esp_timer_get_time()/1000;
    // Expire even if the worker has stopped publishing; never show stale SOC.
    if(out.sampledAtMs<0 || now-out.sampledAtMs>battery::kStaleMs) {
        out.valid=false; out.percent=-1; out.millivolts=0;
    }
    if(out.chargeAtMs<0 || now-out.chargeAtMs>battery::kChargeStaleMs) {
        out.chargingKnown=false; out.charging=false;
    }
    return out;
}
bool Battery::available() { return snapshot().valid; }
float Battery::voltage() { return snapshot().millivolts/1000.0f; }
int Battery::level() { return snapshot().percent; }
supply::State Battery::supplyState() {
    const std::lock_guard<std::mutex> lock(s_supplyMutex);
    return s_supply.snapshot(esp_timer_get_time()/1000);
}
bool Battery::supplyProtected() {return supply::protects(supplyState());}
void Battery::confirmBatterySupply() {s_supplyRequest=1;SystemService::wake();}
bool Battery::canConfirmBatterySupply() {
    const std::lock_guard<std::mutex> lock(s_supplyMutex);
    return s_supply.canConfirm(esp_timer_get_time()/1000);
}
void Battery::clearSupplyConfirmation() {s_supplyRequest=2;SystemService::wake();}
} // namespace sys
