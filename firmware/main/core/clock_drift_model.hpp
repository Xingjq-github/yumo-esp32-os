#pragma once
#include <array>
#include <cstdint>

namespace sys::clock_drift {
constexpr int64_t Second=1000000LL, Hour=3600*Second, MaxInterval=30*24*Hour;
constexpr int64_t MinEpoch=1609459200LL*Second, MaxEpoch=4102444800LL*Second;
constexpr int32_t MaxRate=20000000; // correction parts per billion, +/-2%
struct Model { int32_t ratePpb=0; uint32_t samples=0; };
struct Window { int64_t anchorUs=0, sleepUs=0, appliedUs=0; };
enum class Result { Baseline, Short, Awake, Invalid, Learned };
inline bool valid(Model m) {
    return m.ratePpb>=-MaxRate && m.ratePpb<=MaxRate && m.samples<=65535 && (m.samples || !m.ratePpb);
}
inline bool epochValid(int64_t us) { return us>=MinEpoch && us<MaxEpoch; }
inline int64_t correction(int64_t us,Model m) {
    if(us<0 || us>MaxInterval || !valid(m)) return 0;
    // Avoid overflow even for a month-long sleep at the maximum allowed rate.
    return (us/1000000000LL)*m.ratePpb+(us%1000000000LL)*m.ratePpb/1000000000LL;
}
inline bool addSleep(Window &w,int64_t rawUs,int64_t appliedUs) {
    if(!epochValid(w.anchorUs)) return false;
    if(rawUs<=0 || rawUs>MaxInterval || w.sleepUs>MaxInterval-rawUs ||
       appliedUs>rawUs/50+1 || appliedUs<-(rawUs/50+1)) {w={};return false;}
    w.sleepUs+=rawUs;w.appliedUs+=appliedUs;return true;
}
inline Result synchronize(Model &m,Window &w,int64_t localUs,int64_t networkUs) {
    const Window old=w;
    // Every successful step starts a new observation window, including samples
    // too short to learn from. Offset from BEFORE the step must be supplied.
    w={};if(epochValid(networkUs))w.anchorUs=networkUs;
    if(!valid(m))m={};
    if(!epochValid(localUs)||!epochValid(networkUs))return Result::Invalid;
    if(!epochValid(old.anchorUs))return Result::Baseline;
    const int64_t span=networkUs-old.anchorUs;
    if(span<=0 || span>MaxInterval || old.sleepUs<0 || old.sleepUs>MaxInterval)return Result::Invalid;
    if(old.sleepUs<Hour)return Result::Short;
    const int64_t slept=old.sleepUs+old.appliedUs;
    // Before the first learned correction, a fast RTC legitimately reports more
    // sleep than the network interval. Allow the bounded raw oscillator error.
    if(old.sleepUs-old.sleepUs/50>span+2*Second || slept<0)return Result::Invalid;
    if(slept<span/2)return Result::Awake; // do not attribute long awake drift to sleep
    // Restore the raw RTC error. Omitting appliedUs would learn back toward zero
    // after a good correction and make subsequent offline clocks oscillate.
    const int64_t required=networkUs-localUs+old.appliedUs;
    if(required>old.sleepUs/50 || required<-(old.sleepUs/50))return Result::Invalid;
    const int64_t estimate=static_cast<int64_t>(static_cast<double>(required)*1e9/old.sleepUs);
    if(estimate>MaxRate || estimate<-MaxRate)return Result::Invalid;
    const int divisor=m.samples ? (old.sleepUs>=8*Hour ? 4 : 8) : 1;
    m.ratePpb+=static_cast<int32_t>((estimate-m.ratePpb)/divisor);
    if(m.samples<65535)++m.samples;
    return Result::Learned;
}
using Encoded=std::array<uint8_t,16>;
inline uint32_t read(const Encoded &b,unsigned p) {
    return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);
}
inline void write(Encoded &b,unsigned p,uint32_t n) {for(unsigned i=0;i<4;++i)b[p+i]=n>>(i*8);}
inline uint32_t hash(const Encoded &b) {uint32_t h=2166136261U;for(unsigned i=0;i<12;++i)h=(h^b[i])*16777619U;return h;}
inline Encoded encode(Model m) {
    if(!valid(m))m={};
    Encoded b{};write(b,0,0x31445243U);write(b,4,static_cast<uint32_t>(m.ratePpb));write(b,8,m.samples);write(b,12,hash(b));return b;
}
inline Model decode(const Encoded &b) {
    if(read(b,0)!=0x31445243U || read(b,12)!=hash(b))return {};
    const uint32_t raw=read(b,4);
    Model m{static_cast<int32_t>(raw<=0x7fffffffU ? int64_t(raw) : int64_t(raw)-0x100000000LL),read(b,8)};
    return valid(m)?m:Model{};
}
} // namespace sys::clock_drift
