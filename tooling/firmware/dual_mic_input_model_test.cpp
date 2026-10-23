// The real allocation-free DSP is evaluated by the Xtensa C++ compiler.
#include "core/dual_mic_input.hpp"
#include "core/recording_level.hpp"
#include "core/wav_format.hpp"
#include <array>
using Mic = core::recorder::DualMicInput;
using Level = core::recorder::RecordingLevel;
using Block = std::array<int16_t, Mic::MaxFrames * Mic::Channels>;
constexpr unsigned magnitude(int value) { return value < 0 ? -value : value; }
constexpr void tone(Block &data, int left, int right, int dcLeft=0, int dcRight=0) {
  for(size_t i=0;i<Mic::MaxFrames;++i) {
    const int sign=(i%32)<16?1:-1;
    data[i*2]=static_cast<int16_t>(dcLeft+sign*left);
    data[i*2+1]=static_cast<int16_t>(dcRight+sign*right);
  }
}
constexpr unsigned meanTail(const Block &data) {
  unsigned sum=0;
  for(size_t i=128;i<Mic::MaxFrames;++i)sum+=magnitude(data[i]);
  return sum/(Mic::MaxFrames-128);
}
constexpr bool eitherMicCanRecord() {
  for(unsigned channel=0;channel<2;++channel) {
    Mic mic;Block data{};
    for(unsigned n=0;n<3;++n) {
      tone(data,channel?0:1200,channel?1200:0);
      if(mic.process(data.data(),512)!=512 || mic.selectedChannel()!=channel ||
         meanTail(data)<1000)return false;
    }
    // Previously active microphone disappears; the other one takes over.
    tone(data,channel?1200:0,channel?0:1200);
    mic.process(data.data(),512);
    tone(data,channel?1200:0,channel?0:1200);
    mic.process(data.data(),512);
    if(mic.selectedChannel()==channel || meanTail(data)<1000)return false;
  }
  return true;
}
constexpr bool oppositePhaseDoesNotCancel() {
  Mic mic;Block data{};
  for(unsigned n=0;n<8;++n) {
    tone(data,1200,-1200);
    mic.process(data.data(),512);
    if(mic.selectedChannel()!=0 || meanTail(data)<1000)return false;
  }
  return true;
}
constexpr bool transientDoesNotSwitch() {
  Mic mic;Block data{};
  tone(data,1200,600);mic.process(data.data(),512);
  for(unsigned n=0;n<8;++n) {
    tone(data,1200,n%2?600:8000);mic.process(data.data(),512);
    if(mic.selectedChannel()!=0)return false;
  }
  return true;
}
constexpr bool sustainedStrongerSwitches() {
  Mic mic;Block data{};
  tone(data,600,600);mic.process(data.data(),512);
  for(unsigned n=1;n<=Mic::HoldBlocks;++n) {
    tone(data,600,3000);mic.process(data.data(),512);
    if(mic.selectedChannel()!=(n==Mic::HoldBlocks?1U:0U))return false;
  }
  return meanTail(data)>2500;
}
constexpr bool clippingChoosesUsableOtherMic() {
  Mic mic;Block data{};
  tone(data,2000,900);mic.process(data.data(),512);
  tone(data,32767,900);mic.process(data.data(),512);
  if(mic.selectedChannel()!=1 || meanTail(data)<700 || meanTail(data)>1100)return false;
  // Must not switch back toward a much louder but saturated ADC channel.
  for(unsigned n=0;n<6;++n) {
    tone(data,32767,900);mic.process(data.data(),512);
    if(mic.selectedChannel()!=1)return false;
  }
  return true;
}
constexpr bool independentDcAndSilence() {
  Mic mic;Block data{};
  tone(data,0,0,10000,-7000);
  if(mic.process(data.data(),512)!=512)return false;
  for(size_t i=0;i<512;++i)if(data[i])return false;
  for(unsigned n=0;n<5;++n) {
    tone(data,700,2000,10000,-7000);mic.process(data.data(),512);
  }
  return mic.selectedChannel()==1 && meanTail(data)>1700 && meanTail(data)<2200;
}
constexpr bool resetForPauseMatchesFresh() {
  Mic used,fresh;Level gainUsed,gainFresh;Block a{},b{};
  tone(a,100,6000,9000,-7000);
  used.process(a.data(),512);gainUsed.processDcBlocked(a.data(),512);
  used.reset();gainUsed.reset();
  tone(a,1000,1000,4000,-10000);b=a;
  used.process(a.data(),512);fresh.process(b.data(),512);
  gainUsed.processDcBlocked(a.data(),512);gainFresh.processDcBlocked(b.data(),512);
  return a==b && used.selectedChannel()==fresh.selectedChannel() &&
         gainUsed.gainQ12()==gainFresh.gainQ12();
}
constexpr bool invalidAndShortFramesBounded() {
  Mic mic;Block data{};
  if(mic.process(nullptr,512) || mic.process(data.data(),0) ||
     mic.process(data.data(),513))return false;
  for(size_t frames : {size_t(1),size_t(17),size_t(256)}) {
    mic.reset();tone(data,600,1200);
    data[frames*2]=12345;
    if(mic.process(data.data(),frames)!=frames || data[frames*2]!=12345)return false;
  }
  return true;
}
constexpr bool combinedGainAndMonoTiming() {
  Mic mic;Level level;Block data{};
  for(unsigned n=0;n<16;++n) {
    tone(data,0,600);mic.process(data.data(),512);
    level.processDcBlocked(data.data(),512);
  }
  if(meanTail(data)<3000)return false;
  for(unsigned n=0;n<6;++n) {
    tone(data,n%2?32767:12000,n%2?32767:-12000);
    for(size_t i=0;i<data.size();i+=32)data[i]=-32768;
    const auto frames=mic.process(data.data(),512);
    const auto peak=level.processDcBlocked(data.data(),frames);
    if(frames!=512 || peak>Level::Ceiling)return false;
    for(size_t i=0;i<frames;++i)if(magnitude(data[i])>Level::Ceiling)return false;
  }
  constexpr auto h=core::wav::header(512*sizeof(int16_t));
  uint32_t bytes=0;
  return core::wav::parse(h.data(),44+1024,bytes) && bytes==1024 &&
         bytes*1000/core::wav::ByteRate==32 && h[22]==1 && h[34]==16;
}
constexpr bool dcBlockedPathDoesNotFilterTwice() {
  Level level;std::array<int16_t,512> data{};
  for(auto &value:data)value=1000;
  level.processDcBlocked(data.data(),data.size());
  // An extra DC filter would nearly remove this signal within one block.
  return data.back()>2000 && level.gainQ12()>Level::BaseGain;
}
static_assert(eitherMicCanRecord());
static_assert(oppositePhaseDoesNotCancel());
static_assert(transientDoesNotSwitch());
static_assert(sustainedStrongerSwitches());
static_assert(clippingChoosesUsableOtherMic());
static_assert(independentDcAndSilence());
static_assert(resetForPauseMatchesFresh());
static_assert(invalidAndShortFramesBounded());
static_assert(combinedGainAndMonoTiming());
static_assert(dcBlockedPathDoesNotFilterTwice());
static_assert(sizeof(Mic)<=16 && sizeof(Level)<=16);
