#include "core/hid_input_report.hpp"
#include "core/battery_estimator.hpp"
using sys::hidinput::ReportMap;
using sys::hidinput::Keys;
constexpr uint8_t bootMap[]={0x05,1,0x09,6,0xa1,1,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,0x95,6,0x75,8,0x19,0,0x29,0x65,0x81,0,0xc0};
constexpr bool basic() {
 ReportMap map;if(!map.parse(bootMap,sizeof(bootMap))) return false;
 uint8_t packet[]={2,0,4,0x28,0,0,0,0};Keys keys;
 if(!map.decode(0,packet,sizeof(packet),keys) || !keys.down(0xe1) || !keys.down(4) || !keys.down(0x28) || keys.down(5)) return false;
 packet[2]=1;if(map.decode(0,packet,sizeof(packet),keys) || !keys.down(4)) return false;
 packet[2]=0;packet[3]=0;packet[0]=0;
 if(!map.decode(0,packet,sizeof(packet),keys) || keys.down(4) || keys.down(0xe1)) return false;
 return !map.decode(0,packet,7,keys) && !map.decode(99,packet,8,keys);
}
constexpr bool nkro() {
 // Report 7: 3 constant leading bits + 100 keyboard bits, spanning byte boundaries.
 const uint8_t desc[]={0x85,7,0x05,7,0x75,1,0x95,3,0x81,1,0x19,4,0x29,103,0x95,100,0x81,2};
 ReportMap map;uint8_t bytes[13]{};Keys keys;
 if(!map.parse(desc,sizeof(desc))) return false;
 bytes[0]=8;bytes[12]=0x40;
 return map.decode(7,bytes,13,keys) && keys.down(4) && keys.down(103) && !keys.down(5);
}
constexpr bool multi() {
 const uint8_t desc[]={0x05,7,0x85,1,0x75,8,0x95,1,0x19,4,0x29,100,0x81,0,0x85,2,0x75,1,0x95,8,0x19,0xe0,0x29,0xe7,0x81,2};
 ReportMap map;uint8_t data[]={4};Keys keys;
 if(!map.parse(desc,sizeof(desc)) || !map.decode(1,data,1,keys) || !keys.down(4)) return false;
 return map.decode(2,data,1,keys) && keys.down(0xe2) && !keys.down(4);
}
constexpr bool malformed() {
 ReportMap map;const uint8_t truncated[]={0x75};const uint8_t longItem[]={0xfe,0,0};
 const uint8_t tooWide[]={0x05,7,0x75,32,0x96,0,2,0x81,0};
 const uint8_t badID[]={0x85,0};const uint8_t pop[]={0xb4};
 return !map.parse(nullptr,1) && !map.parse(bootMap,0) && !map.parse(bootMap,2049) &&
 !map.parse(truncated,1) && !map.parse(longItem,3) && !map.parse(tooWide,sizeof(tooWide)) && !map.parse(badID,2) && !map.parse(pop,1);
}
using sys::battery::Estimator;
constexpr void feed(Estimator &e,int from,int to,int mv,bool charging) {
 for(int t=from;t<=to;t+=1000) {e.observeCharge(charging,t);if(t%5000==0) e.observeVoltage(mv,t);e.advance(t);}
}
constexpr bool batterySmallChange() {
 Estimator e;feed(e,0,60000,4000,true);if(e.snapshot(60000).percent!=85) return false;
 feed(e,61000,1200000,4006,true);return e.snapshot(1200000).percent==86;
}
constexpr bool batteryFull() {
 Estimator e;feed(e,0,300000,4110,true);
 if(e.snapshot(300000).percent>=100) return false;
 feed(e,301000,1200000,4110,false);return e.snapshot(1200000).percent==100;
}
constexpr bool batteryLimits() {
 Estimator e;feed(e,0,10000,3800,false);
 auto before=e.snapshot(10000);e.observeVoltage(0,12000);e.observeVoltage(5000,14000);
 if(e.snapshot(15000).percent!=before.percent || e.snapshot(70001).valid) return false;
 e.observeVoltage(3500,75000);if(e.snapshot(75000).percent!=5 || e.snapshot(75000).chargingKnown) return false;
 feed(e,76000,600000,3300,false);return e.snapshot(600000).percent==0;
}
constexpr bool batteryNearFullNotForced() {
 Estimator e;feed(e,0,600000,4090,false);if(e.snapshot(600000).percent!=98) return false;
 Estimator below;feed(below,0,600000,4109,false);if(below.snapshot(600000).percent!=99) return false;
 Estimator cold;feed(cold,0,600000,4110,false);return cold.snapshot(600000).percent==100;
}
constexpr bool batteryDebounce() {
 Estimator e;for(int t=0;t<5000;t+=500) e.observeCharge(t%1000==0,t);
 if(e.snapshot(5000).chargingKnown) return false;
 e.observeCharge(true,6000);e.observeCharge(true,8000);
 return e.snapshot(8000).charging && !e.snapshot(13001).chargingKnown;
}
static_assert(basic(),"6KRO, modifiers, release, rollover, short reports");
static_assert(nkro(),"NKRO bit offsets");
static_assert(multi(),"multiple report IDs");
static_assert(malformed(),"reject malformed / oversized / unsupported descriptors");
static_assert(batterySmallChange(),"fractional display progress must converge for a one-point change");
static_assert(batteryFull(),"qualified plateau must reach full after charge stops");
static_assert(batteryLimits(),"invalid/stale ADC, reseed, empty endpoint");
static_assert(batteryNearFullNotForced(),"only a qualified display endpoint reaches full, including after cold boot");
static_assert(batteryDebounce(),"charge debounce and freshness");
