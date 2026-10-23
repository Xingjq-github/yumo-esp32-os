// Execute the production estimator against time-series inputs, without hardware.
#include "core/battery_estimator.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#ifdef BATTERY_PREVIOUS
#include "battery_previous.hpp"
#endif

using sys::battery::Estimator;
using sys::battery::percentForVoltage;
static unsigned checks=0;
static void check(bool ok,const char *message) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
static int tick(Estimator &e,int64_t t,int mv,bool charging=false,int period=5000) {
    e.observeCharge(charging,t);
    if(t%period==0)e.observeVoltage(mv,t);
    e.advance(t);
    return e.snapshot(t).percent;
}
static void hold(Estimator &e,int64_t from,int64_t to,int mv,bool charging=false,int period=5000) {
    for(auto t=from;t<=to;t+=1000)tick(e,t,mv,charging,period);
}
static int onePointLatency(int period) {
    Estimator e;hold(e,0,60000,4000,false,period);
    const int initial=e.snapshot(60000).percent;
    check(initial-percentForVoltage(3995)==1,"latency trace must request exactly one point");
    int latency=-1;
    for(int t=61000;t<=240000;t+=1000) {
        const int previous=e.snapshot(t).percent;
        const int p=tick(e,t,3995,false,period);
        check(p<=previous && previous-p<=1,"one-point discharge must not jump or rise");
        if(p<initial && latency<0)latency=t-60000;
    }
    check(latency>0 && latency<=90000,"sustained one-point decrease must appear within 90 seconds");
    check(e.snapshot(240000).percent==percentForVoltage(3995),"one-point decrease must converge");
    return latency;
}
static void endpointsAndFreshness() {
    int previous=-1;
    for(int mv=2500;mv<=4600;++mv) {
        const int p=percentForVoltage(mv);
        check(p>=previous && p>=0 && p<=100,"voltage curve must be bounded and monotonic");
        previous=p;
    }
    check(percentForVoltage(3300)==0 && percentForVoltage(4110)==100,"display endpoints retained");
    check(percentForVoltage(3800)>=35 && percentForVoltage(3800)<=45,"middle range must be conservative");
    for(int mv=3700;mv<=3900;++mv)
        check(percentForVoltage(mv+100)-percentForVoltage(mv)<=24,"middle curve must not pack 33 points into 100mV");
    for(int period:{5000,15000}) {
        Estimator e;
        check(!e.snapshot(0).valid && e.snapshot(0).percent==-1,"no synthetic battery before measurement");
        hold(e,0,120000,4116,false,period);
        check(e.snapshot(120000).percent==99,"full needs the existing qualification time");
        hold(e,121000,300000,4116,false,period);
        check(e.snapshot(300000).percent==100,"qualified full endpoint is reachable");
        hold(e,301000,340000,4200,true,period);
        check(e.snapshot(340000).charging && e.snapshot(340000).percent<=99,"charger insertion is not proof of full");
        const int before=e.snapshot(340000).percent;
        e.advance(340000);e.advance(339000);
        check(e.snapshot(340000).percent==before,"duplicate/backward ticks earn no progress");
        e.observeVoltage(0,350000);e.observeVoltage(5000,355000);e.advance(500000);
        check(!e.snapshot(500000).valid && e.snapshot(500000).percent==-1,"invalid/stale samples cannot invent charge");
        e.observeVoltage(3500,500001);
        check(e.snapshot(500001).percent==5 && !e.snapshot(500001).chargingKnown,"stale recovery reseeds and expires charging");
        Estimator below;hold(below,0,600000,4109,false,period);
        check(below.snapshot(600000).percent==99,"rounding below full cannot claim full");
    }
}
static void noiseAndLoad() {
    for(int period:{5000,15000}) {
        Estimator stable;int low=100,high=0;
        for(int t=0;t<=1800000;t+=1000) {
            const int p=tick(stable,t,3800+(t/period%2?3:-3),false,period);
            low=std::min(low,p);high=std::max(high,p);
        }
        check(high-low<=1,"ADC-scale noise must not flicker the display");
        Estimator pulse;hold(pulse,0,60000,4000,false,period);
        const int start=pulse.snapshot(60000).percent;low=start;
        for(int t=61000;t<=180000;t+=1000) {
            const int p=tick(pulse,t,t==75000?3700:4000,false,period);
            low=std::min(low,p);
        }
        check(start-low<=1,"one load pulse must not be treated as deep discharge");
        Estimator rebound;hold(rebound,0,60000,3700,false,period);
        const int initial=rebound.snapshot(60000).percent;
        hold(rebound,61000,360000,3900,false,period);
        check(rebound.snapshot(360000).percent-initial<=3,"rest voltage recovery cannot look like rapid charging");
        Estimator charging;hold(charging,0,60000,3800,false,period);
        hold(charging,61000,120000,4200,true,period);
        check(charging.snapshot(120000).percent<=42,"insertion must not jump to the charging voltage estimate");
    }
}
static int lowVoltage(int period) {
    // Finish full qualification and the slow upward display transition before
    // starting this discharge-only trace, for both sampling cadences.
    constexpr int start=300000;
    Estimator e;hold(e,0,start,4116,false,period);
    int previous=e.snapshot(start).percent,emptyAt=-1;
    check(previous==100,"low-voltage trace must start from settled full display");
    for(int t=start+1000;t<=start+720000;t+=1000) {
        const int p=tick(e,t,3300,false,period);
        if(p>previous || previous-p>1)
            std::fprintf(stderr,"low voltage: period=%d t=%d previous=%d current=%d\n",period,t,previous,p);
        check(p<=previous && previous-p<=1,"low voltage must descend one displayed point at a time");
        if(p==0 && emptyAt<0)emptyAt=t-start;
        previous=p;
    }
    check(emptyAt>0 && emptyAt<=640000,"sustained empty voltage must not be hidden by smoothing");
    return emptyAt;
}
static void slowDischarge() {
    Estimator lit,dark;double lagSum=0;int maxLag=0,count=0,maxGap=0;
    int litPrevious=-1,darkPrevious=-1;
    // This is a responsiveness trace, not a claim that voltage falls linearly
    // with real capacity. Compare the display with its own current voltage target.
    for(int t=0;t<=4*3600000;t+=1000) {
        const int mv=4050-static_cast<int>(450LL*t/(4*3600000));
        const int a=tick(lit,t,mv,false,5000),b=tick(dark,t,mv,false,15000);
        if(t) {
            check(a<=litPrevious && litPrevious-a<=1,"slow lit discharge must be monotonic and single-step");
            check(b<=darkPrevious && darkPrevious-b<=1,"slow dark discharge must be monotonic and single-step");
        }
        if(t>=60000) {
            const int lag=a-percentForVoltage(mv);
            maxLag=std::max(maxLag,lag);lagSum+=lag;++count;
            maxGap=std::max(maxGap,std::abs(a-b));
        }
        litPrevious=a;darkPrevious=b;
    }
    check(maxLag<=2 && lagSum/count<1.0,"slow discharge must not accumulate a large delayed correction");
    check(maxGap<=1,"lit/dark sampling should give the same display to within one point");
    std::printf("slow_discharge_max_lag=%d mean_lag=%.3f lit_dark_gap=%d\n",maxLag,lagSum/count,maxGap);
}
static void randomTrace() {
    std::mt19937 rng(20261009);Estimator e;int previous=-1;
    for(int t=0;t<=3600000;t+=1000) {
        const int mv=3300+static_cast<int>(rng()%900);
        const int p=tick(e,t,mv,(t/120000)%2!=0);
        check(p>=0 && p<=100,"random trace must remain in display bounds");
        if(previous>=0)check(std::abs(p-previous)<=1,"worker ticks cannot create a multi-point jump");
        previous=p;
    }
}
#ifdef BATTERY_PREVIOUS
static int previousLatency() {
    previous_sys::battery::Estimator e;int initial=0;
    for(int t=0;t<=60000;t+=1000) {
        e.observeCharge(false,t);if(t%5000==0)e.observeVoltage(4000,t);
    }
    initial=e.snapshot(60000).percent;
    check(initial-previous_sys::battery::percentForVoltage(3995)==1,"baseline trace must also request exactly one point");
    for(int t=61000;t<=300000;t+=1000) {
        e.observeCharge(false,t);if(t%5000==0)e.observeVoltage(3995,t);
        if(e.snapshot(t).percent<initial)return t-60000;
    }
    return -1;
}
#endif
int main() {
    endpointsAndFreshness();noiseAndLoad();slowDischarge();randomTrace();
    const int lit=onePointLatency(5000),dark=onePointLatency(15000);
    std::printf("one_point_latency_ms: lit=%d dark=%d\n",lit,dark);
    std::printf("empty_convergence_ms: lit=%d dark=%d\n",lowVoltage(5000),lowVoltage(15000));
#ifdef BATTERY_PREVIOUS
    const int old=previousLatency();
    check(old>lit,"the regression trace must improve on the previous implementation");
    std::printf("previous_one_point_latency_ms=%d\n",old);
#endif
    std::printf("PASS: %u actual C++ estimator checks\n",checks);
}
