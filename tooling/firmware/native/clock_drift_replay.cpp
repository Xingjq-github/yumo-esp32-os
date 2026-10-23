#include "core/clock_drift_model.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace sys::clock_drift;
int main() {
    const int64_t epoch=1800000000LL*Second;
    Model m;Window w;
    CHECK(synchronize(m,w,epoch,epoch)==Result::Baseline);
    const int64_t raw=16*Hour+240*Second;
    CHECK(addSleep(w,raw,0));
    CHECK(synchronize(m,w,epoch+raw,epoch+16*Hour)==Result::Learned);
    CHECK(std::abs(m.ratePpb+4149377)<2 && m.samples==1);
    const auto initial=m.ratePpb;
    for(int i=0;i<100;++i) {
        const auto anchor=w.anchorUs;
        const auto applied=correction(raw,m);
        CHECK(std::llabs(raw+applied-16*Hour)<100);
        CHECK(addSleep(w,raw,applied));
        CHECK(synchronize(m,w,anchor+raw+applied,anchor+16*Hour)==Result::Learned);
        CHECK(std::abs(m.ratePpb-initial)<=2); // never unlearn existing compensation
    }
    // Multiple sleep intervals are accumulated; awake seconds are not scaled.
    const auto anchor=w.anchorUs;
    for(int i=0;i<4;++i)CHECK(addSleep(w,raw/4,correction(raw/4,m)));
    CHECK(synchronize(m,w,anchor+raw+correction(raw,m)+60*Second,anchor+16*Hour+60*Second)==Result::Learned);
    CHECK(std::abs(m.ratePpb-initial)<=2);
    // Negative raw drift needs a positive correction, with no sign inversion.
    m={};w={epoch,0,0};
    CHECK(addSleep(w,16*Hour-120*Second,0));
    CHECK(synchronize(m,w,epoch+16*Hour-120*Second,epoch+16*Hour)==Result::Learned);
    CHECK(m.ratePpb>2000000 && m.ratePpb<2200000);
    for(int rate=-19000000;rate<=19000000;rate+=100000) {
        Model sample{rate,10};const auto encoded=encode(sample);CHECK(decode(encoded).ratePpb==rate);
        for(int64_t duration: {Second,Hour,16*Hour,MaxInterval})
            CHECK(std::llabs(correction(duration,sample)-static_cast<int64_t>(static_cast<long double>(duration)*rate/1e9L))<=1);
    }
    // Byte corruption, future schema, invalid rates/counts cannot reach the clock.
    auto encoded=encode(m);
    for(unsigned i=0;i<encoded.size();++i)for(unsigned bit=0;bit<8;++bit){auto b=encoded;b[i]^=1U<<bit;CHECK(decode(b).samples==0);}
    CHECK(!valid({MaxRate+1,1}));CHECK(!valid({1,0}));CHECK(!valid({0,65536}));
    CHECK(correction(MaxInterval+1,m)==0);CHECK(correction(-1,m)==0);
    const auto saved=m;
    w={epoch,Hour-1,0};CHECK(synchronize(m,w,epoch+Hour,epoch+Hour)==Result::Short);
    CHECK(w.sleepUs==0&&w.anchorUs==epoch+Hour);
    w={epoch,Hour,0};CHECK(synchronize(m,w,epoch+4*Hour,epoch+4*Hour)==Result::Awake);
    w={epoch,2*Hour,0};CHECK(synchronize(m,w,epoch+2*Hour+1000*Second,epoch+2*Hour)==Result::Invalid);
    w={epoch,2*Hour,0};CHECK(synchronize(m,w,epoch+2*Hour,epoch-Hour)==Result::Invalid);
    w={epoch,2*Hour,0};CHECK(synchronize(m,w,epoch+2*Hour,epoch+MaxInterval+1)==Result::Invalid);
    CHECK(m.ratePpb==saved.ratePpb&&m.samples==saved.samples);
    w={};CHECK(!addSleep(w,Hour,0));
    w={epoch,MaxInterval,0};CHECK(!addSleep(w,Second,0)&&w.anchorUs==0);
    // Temperature changes adapt gradually after the first complete sample.
    m={-4000000,1};w={epoch,16*Hour,0};
    CHECK(synchronize(m,w,epoch+16*Hour,epoch+16*Hour-16*Hour/1000)==Result::Learned);
    CHECK(m.ratePpb==-3250000);
    std::printf("%u clock drift model checks passed\n",checks);
}
