/* Production frame scheduling/output, fed by the real PPU fixture. */
#include "core/fc_timing.hpp"
#include "core/fc_video.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <vector>
using namespace core::fc;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
constexpr unsigned Pitch=272, Overdraw=8, FrameBytes=Pitch*240;
static std::array<uint8_t,FrameBytes*2> fixture{};
static std::array<uint8_t,FrameBytes> source{};
static std::array<uint16_t,256> palette{};
static std::array<uint16_t,Pixels+2> output{};

static void scale(const uint8_t *data)
{
    output.front()=0xA55A;output.back()=0x5AA5;
    scaleFrame(output.data()+1,data,Pitch,Overdraw,palette.data());
    CHECK(output.front()==0xA55A && output.back()==0x5AA5);
}
static bool hasLetter()
{
    for(unsigned y=74;y<88;++y)for(unsigned x=72;x<91;++x)
        if(output[1+y*Width+x]==0xFFFF)return true;
    return false;
}
static void schedule(unsigned hz,unsigned consumeEvery,unsigned consumePhase,
                     unsigned rejectEvery,bool delayed)
{
    FrameTiming timing(hz);timing.reset(0);
    bool pending=false,visible=false;unsigned pendingPhase=0;
    unsigned seen[2]{},draws=0,attempts=0,rejected=0,lastSeen[2]{},largestGap[2]{};
    int64_t now=0;
    for(unsigned f=0;f<hz*12;++f) {
        if(f%consumeEvery==consumePhase && pending) {
            CHECK(visible==bool(pendingPhase));
            ++seen[pendingPhase];
            if(lastSeen[pendingPhase]) {
                unsigned gap=f-lastSeen[pendingPhase];
                if(gap>largestGap[pendingPhase])largestGap[pendingPhase]=gap;
            }
            lastSeen[pendingPhase]=f;pending=false;
        }
        int64_t start=now+(delayed && f%43<3 ? 2*(1000000/hz) : 0);
        if(timing.draw(start,!pending)) {
            ++attempts;
            if(rejectEvery && ++rejected%rejectEvery==0) {
                // A failed producer try_lock must not count as presentation.
            } else {
                scale(fixture.data()+(f%2)*FrameBytes);
                visible=hasLetter();pendingPhase=f%2;pending=true;
                timing.presented(start);++draws;
            }
        }
        int64_t done=start+4000;
        now=done+timing.finish(done);
    }
#ifdef EXPECT_OLD
    if(consumeEvery==1 && !rejectEvery && !delayed) {
        CHECK(seen[0]>hz*4);CHECK(seen[1]==0);
    }
#else
    CHECK(seen[0]>hz && seen[1]>hz);
    CHECK(largestGap[0]<=24 && largestGap[1]<=24);
    CHECK(draws<=hz*6+2);
    CHECK(attempts<=hz*6+2);
    if(consumeEvery==1 && !rejectEvery && !delayed) {
        CHECK(draws<=hz*6 && draws>=hz*5.6);
        CHECK(seen[0]>hz*2 && seen[1]>hz*2);
        CHECK(largestGap[0]<=12 && largestGap[1]<=12);
        CHECK(now==12000000);
    }
#endif
}
static unsigned missingLines()
{
    palette.fill(0);palette[1]=0xFFFF;
    unsigned missing=0;
    for(unsigned axis=0;axis<2;++axis)for(unsigned at=0;at<(axis?240:256);++at) {
        source.fill(0);
        if(axis) std::memset(source.data()+at*Pitch+Overdraw,1,256);
        else for(unsigned y=0;y<240;++y)source[y*Pitch+Overdraw+at]=1;
        scale(source.data());
        bool any=false;
        for(unsigned i=1;i<=Pixels;++i)any=any||output[i]!=0;
#if !defined(EXPECT_OLD) && !defined(EXPECT_AREA)
        // Only the symmetric eight-column borders may disappear. No inner
        // columns or rows may be dropped, diluted or duplicated.
        const bool visible=axis || (at>=8 && at<248);
        CHECK(any==visible);
        unsigned lit=0;
        for(unsigned i=1;i<=Pixels;++i) {
            CHECK(output[i]==0 || output[i]==0xFFFF);
            lit+=output[i]!=0;
        }
        CHECK(lit==(visible?240u:0u));
#endif
        missing+=!any;
    }
#ifdef EXPECT_OLD
    CHECK(missing==31);
#elif defined(EXPECT_AREA)
    CHECK(missing==0);
#else
    CHECK(missing==16);
#endif
    return missing;
}
#ifndef EXPECT_OLD
static void rotatingOam()
{
    for(unsigned cadence=1;cadence<=4;++cadence)for(unsigned offset=0;offset<cadence;++offset) {
        FrameTiming timing(60);timing.reset(0);int64_t now=0;
        int pending=-1;unsigned seen[33][32]{};
        for(unsigned f=0;f<18000;++f) {
            if(pending>=0 && f%cadence==offset) {
                for(unsigned period=2;period<=32;++period)++seen[period][pending%period];
                pending=-1;
            }
            if(timing.draw(now,pending<0)) {pending=f;timing.presented(now);}
            now+=4000;now+=timing.finish(now);
        }
        for(unsigned period=2;period<=32;++period)for(unsigned p=0;p<period;++p) {
            if(!seen[period][p]) std::fprintf(stderr,"cadence=%u offset=%u period=%u phase=%u\n",cadence,offset,period,p);
            CHECK(seen[period][p]>0);
        }
    }
}
#ifdef EXPECT_AREA
static unsigned reference(uint16_t a,uint16_t b,uint16_t c,uint16_t d,
                          unsigned wx,unsigned wy,unsigned shift,unsigned mask)
{
    const unsigned top=((a>>shift)&mask)*(16-wx)+((b>>shift)&mask)*wx;
    const unsigned bottom=((c>>shift)&mask)*(16-wx)+((d>>shift)&mask)*wx;
    return (top*(16-wy)+bottom*wy)/256;
}
static void filtering()
{
    unsigned seed=0x719AC;
    for(auto &c:palette) {seed=seed*1664525u+1013904223u;c=seed>>16;}
    for(auto &s:source) {seed=seed*1664525u+1013904223u;s=seed>>24;}
    scale(source.data());
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x) {
        unsigned sx=x*256/Width,sy=y*240/Height;
        unsigned wx=x%15+1,wy=y%15+1;
        const auto p=source.data()+sy*Pitch+Overdraw+sx;
        const auto a=palette[p[0]],b=palette[p[1]],c=palette[p[Pitch]],d=palette[p[Pitch+1]];
        const auto pixel=output[1+y*Width+x];
        for(unsigned channel=0;channel<3;++channel) {
            unsigned shift=channel==0?0:channel==1?5:11;
            unsigned mask=channel==1?63:31;
            int expected=reference(a,b,c,d,wx,wy,shift,mask);
            int actual=(pixel>>shift)&mask;
            CHECK(actual<=expected && expected-actual<=1);
        }
    }
    for(unsigned index=0;index<256;++index) {
        source.fill(index);scale(source.data());
        for(unsigned i=1;i<=Pixels;i+=239)CHECK(output[i]==palette[index]);
    }
}
#else
static void exactPixels()
{
    CHECK(Width==240 && Height==240 && CropX==8 && CropY==0);
    unsigned seed=0x719AC;
    for(auto &c:palette) {seed=seed*1664525u+1013904223u;c=seed>>16;}
    // Exercise both the real padded PPU format and tightly packed scanlines.
    for(unsigned pitch:{256u,272u,288u}) {
        const unsigned padding=(pitch-256)/2;
        std::vector<uint8_t> input(pitch*240);
        for(auto &s:input) {seed=seed*1664525u+1013904223u;s=seed>>24;}
        const auto original=input;
        output.front()=0xA55A;output.back()=0x5AA5;
        scaleFrame(output.data()+1,input.data(),pitch,padding,palette.data());
        CHECK(output.front()==0xA55A && output.back()==0x5AA5);
        CHECK(input==original);
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x)
            CHECK(output[1+y*240+x]==palette[input[y*pitch+padding+x+8]]);
    }
    // Adjacent contrasting pixels must remain crisp, with no colour mixing.
    palette.fill(0);palette[1]=0xFFFF;
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<256;++x)
        source[y*Pitch+Overdraw+x]=(x+y)&1;
    scale(source.data());
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x)
        CHECK(output[1+y*240+x]==(((x+y)&1)?0xFFFF:0));
    for(unsigned index=0;index<256;++index) {
        palette[index]=uint16_t(index*257);source.fill(index);scale(source.data());
        for(unsigned i=1;i<=Pixels;i+=239)CHECK(output[i]==palette[index]);
    }
}
#endif
#endif
int main(int argc,char **argv)
{
    CHECK(argc==2);
    FILE *file=std::fopen(argv[1],"rb");CHECK(file);
    CHECK(std::fread(fixture.data(),1,fixture.size(),file)==fixture.size());std::fclose(file);
    for(unsigned i=0;i<256;++i)palette[i]=(i&63)==17?0xFFFF:0;
    scale(fixture.data());CHECK(!hasLetter());
    scale(fixture.data()+FrameBytes);CHECK(hasLetter());
    for(unsigned hz:{50u,60u})for(unsigned cadence:{1u,2u,3u})
        for(unsigned phase=0;phase<cadence;++phase)for(unsigned reject:{0u,5u})
            for(bool late:{false,true})schedule(hz,cadence,phase,reject,late);
    unsigned missing=missingLines();
#ifndef EXPECT_OLD
    rotatingOam();
#ifdef EXPECT_AREA
    filtering();
#else
    exactPixels();
#endif
#endif
    // Same benchmark input for both implementations. Host timings only.
    unsigned seed=0x731AC;
    for(auto &c:palette) {seed=seed*1664525u+1013904223u;c=seed>>16;}
    for(auto &s:source) {seed=seed*1664525u+1013904223u;s=seed>>24;}
    // Median of seven samples; checksum every frame and vary input so the
    // compiler cannot replace repeated conversion with one cached result.
    std::array<long long,7> samples{};
    unsigned checksum=0;
    for(unsigned n=0;n<16;++n)scale(source.data());
    for(auto &us:samples) {
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<300;++n) {
            source[(n*977)%FrameBytes]^=uint8_t(n);
            scale(source.data());checksum+=output[1+n%Pixels];
        }
        us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    }
    std::sort(samples.begin(),samples.end());
    std::printf("{\"passed\":true,\"checks\":%u,\"missing_source_lines\":%u,\"host_scale_300_frames_us\":%lld,\"benchmark_samples\":7,\"output_pixels\":%u,\"checksum\":%u}\n",checks,missing,samples[3],Pixels,checksum);
}
