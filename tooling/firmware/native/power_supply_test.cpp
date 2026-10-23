// Actual Battery implementation, fake GPIO/USB/ADC/clock; no device access.
#include "core/battery_mgr.hpp"
#include "core/power_policy.hpp"
#include <atomic>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <thread>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}} while(0)
namespace fake {
std::atomic<int64_t> ms{0};std::atomic<bool> usb{false},charge{false};
std::atomic<unsigned> reads{0},activities{0},wakes{0};
unsigned adcReads=0;bool adcFails=false;
}
using esp_err_t=int;constexpr int ESP_OK=0;
using adc_unit_t=int;using adc_channel_t=int;using adc_atten_t=int;using gpio_num_t=int;
constexpr int ADC_UNIT_1=1,ADC_CHANNEL_0=0,ADC_ATTEN_DB_6=6,GPIO_NUM_3=3;
constexpr int GPIO_MODE_INPUT=0,GPIO_PULLUP_ENABLE=1,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0;
constexpr int ADC_ULP_MODE_DISABLE=0,ADC_BITWIDTH_DEFAULT=12;
using adc_oneshot_unit_handle_t=void*;using adc_cali_handle_t=void*;
struct gpio_config_t {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;};
struct adc_oneshot_unit_init_cfg_t {int unit_id,ulp_mode;};
struct adc_oneshot_chan_cfg_t {int bitwidth,atten;};
struct adc_cali_curve_fitting_config_t {int unit_id,chan,atten,bitwidth;};
int gpio_config(gpio_config_t*){return 0;}
int gpio_get_level(int){++fake::reads;return !fake::charge;}
bool usb_serial_jtag_is_connected(){return fake::usb;}
int64_t esp_timer_get_time(){return fake::ms.load()*1000;}
int adc_oneshot_new_unit(adc_oneshot_unit_init_cfg_t*,void**p){*p=(void*)1;return 0;}
int adc_oneshot_config_channel(void*,int,adc_oneshot_chan_cfg_t*){return 0;}
int adc_oneshot_del_unit(void*){return 0;}
int adc_cali_create_scheme_curve_fitting(adc_cali_curve_fitting_config_t*,void**p){*p=(void*)1;return 0;}
int adc_oneshot_read(void*,int,int*p){++fake::adcReads;*p=1300;return fake::adcFails?1:0;}
int adc_cali_raw_to_voltage(void*,int raw,int*p){*p=raw;return 0;}
#define ESP_LOGW(...) ((void)0)
namespace sys {
struct SystemService {static void wake(){++fake::wakes;}};
struct Power {static void activity(){++fake::activities;(void)Battery::supplyState();}};
}
#include "battery.inc"
using sys::Battery;
using sys::supply::State;
void reset() {
    sys::s_supply={};sys::s_nextSupply=0;sys::s_supplyRequest=0;
    fake::ms=0;fake::usb=false;fake::charge=false;fake::reads=0;fake::activities=0;
}
void batteryAtStart() {
    reset();Battery::pollSupply();fake::ms=1000;Battery::pollSupply();
    fake::ms=2000;CHECK(Battery::canConfirmBatterySupply());
    Battery::confirmBatterySupply();Battery::pollSupply();
    CHECK(Battery::supplyState()==State::BatteryConfirmed);
}
void automaticBatteryNight() {
    reset();
    sys::power::Policy policy;sys::power::Config cfg;sys::power::Input input;
    input.screen=false;
    int standbyAt=-1,deepAt=-1;
    for(int t=0;t<=28800000;t+=1000) {
        fake::ms=t;Battery::pollSupply();
        input.now=t;input.activity=fake::activities;
        input.supplyProtected=Battery::supplyProtected();
        const auto d=policy.step(cfg,input);
        CHECK(input.supplyProtected==(t<30000));
        if(d.stage==sys::power::Stage::Standby && standbyAt<0)standbyAt=t;
        if(d.deep && deepAt<0)deepAt=t;
    }
    CHECK(standbyAt==150000 && deepAt==270000); // 30s inference + independent dark deadlines
    CHECK(Battery::supplyState()==State::BatteryInferred);
    // Explicit full-charger protection survives well beyond the inference delay.
    Battery::clearSupplyConfirmation();Battery::pollSupply();
    for(int t=28801000;t<=28900000;t+=1000) {
        fake::ms=t;Battery::pollSupply();CHECK(Battery::supplyState()==State::Protected);
    }
    Battery::confirmBatterySupply();Battery::pollSupply();CHECK(!Battery::supplyProtected());
    fake::usb=true;Battery::poll(false);CHECK(Battery::supplyState()==State::External);
    fake::usb=false;
    for(int t=28901000;t<28931000;t+=1000) {
        fake::ms=t;Battery::pollSupply();CHECK(Battery::supplyProtected());
    }
    fake::ms=28931000;Battery::pollSupply();CHECK(Battery::supplyState()==State::BatteryInferred);
    // CHG overrides inferred battery immediately in the final deep-sleep check.
    fake::charge=true;Battery::poll(false);CHECK(Battery::supplyState()==State::External);
    // Cover the real sampling path with all insertion/charging and setting combinations.
    for(unsigned mask=0;mask<16;++mask) {
        reset();fake::usb=mask&1;fake::charge=mask&2;
        sys::power::Policy p;sys::power::Config c;sys::power::Input i;
        c.sleepOnPower=bool(mask&4);c.screenOffOnPower=bool(mask&8);
        bool black=false,deep=false;
        for(int t=0;t<=600000;t+=1000) {
            fake::ms=t;Battery::pollSupply();i.now=t;i.supplyProtected=Battery::supplyProtected();
            auto d=p.step(c,i);
            if(d.black){black=true;i.screen=false;p.step(c,i);}
            deep=deep||d.deep;
        }
        const bool external=bool(mask&3);
        CHECK(black==(!external||c.screenOffOnPower));
        CHECK(deep==(black&&(!external||c.sleepOnPower)));
    }
}
int main() {
    CHECK(Battery::init());CHECK(Battery::snapshot().valid);
    reset();Battery::pollSupply();CHECK(Battery::supplyProtected());
    for(int t=20;t<1000;t+=20){fake::ms=t;Battery::pollSupply();}
    CHECK(fake::reads==1); // GUI frequency must not become hardware polling frequency.
    fake::ms=1999;Battery::confirmBatterySupply();Battery::pollSupply();
    CHECK(Battery::supplyProtected());
    fake::ms=2000;Battery::confirmBatterySupply();Battery::pollSupply();
    CHECK(Battery::supplyState()==State::BatteryConfirmed);
    CHECK(fake::activities==1);
    fake::usb=true;Battery::confirmBatterySupply();Battery::pollSupply();
    CHECK(Battery::supplyState()==State::External);CHECK(fake::activities==2);
    fake::usb=false;fake::ms=3000;Battery::pollSupply();CHECK(Battery::supplyProtected());
    fake::ms=5000;Battery::confirmBatterySupply();Battery::pollSupply();
    CHECK(!Battery::supplyProtected());
    // Force-before-sleep bypasses rate limiting and sees power inserted just now.
    fake::charge=true;fake::ms=5001;Battery::poll(false);
    CHECK(Battery::supplyState()==State::External);

    batteryAtStart();
    sys::power::Policy policy;sys::power::Config cfg;sys::power::Input input;
    input.now=2000;input.activity=fake::activities;policy.step(cfg,input);
    bool black=false,deep=false;
    const auto adcBefore=fake::adcReads;
    // No SystemService/Battery::poll for five minutes: GUI supply checks remain
    // live, keep the explicit confirmation, and preserve the full sleep timeline.
    for(int t=2020;t<=302000;t+=20) {
        fake::ms=t;Battery::pollSupply();CHECK(!Battery::supplyProtected());
        input.now=t;input.activity=fake::activities;input.supplyProtected=Battery::supplyProtected();
        auto decision=policy.step(cfg,input);
        if(decision.black){CHECK(t==62000);black=true;input.screen=false;policy.step(cfg,input);}
        if(decision.deep){CHECK(t==302000);deep=true;}
    }
    CHECK(black&&deep);CHECK(fake::adcReads==adcBefore);
    fake::ms=305001;CHECK(Battery::supplyProtected()); // both samplers stopped
    Battery::pollSupply();CHECK(Battery::supplyProtected()); // fail closed, not inferred battery
    fake::ms=307001;Battery::confirmBatterySupply();Battery::pollSupply();CHECK(!Battery::supplyProtected());
    Battery::clearSupplyConfirmation();Battery::pollSupply();CHECK(Battery::supplyProtected());
    batteryAtStart();fake::adcFails=true;Battery::poll(false);CHECK(!Battery::supplyProtected());fake::adcFails=false;
    // Simultaneous GUI/worker supply access must publish one coherent monitor.
    std::thread a([]{for(int i=0;i<10000;++i)Battery::pollSupply(true);});
    std::thread b([]{for(int i=0;i<10000;++i){Battery::pollSupply();(void)Battery::canConfirmBatterySupply();}});
    a.join();b.join();CHECK(!Battery::supplyProtected());
    fake::charge=true;Battery::pollSupply(true);CHECK(Battery::supplyState()==State::External);
    automaticBatteryNight();
    std::cout<<checks<<" supply/policy checks passed\n";
}
