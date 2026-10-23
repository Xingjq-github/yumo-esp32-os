#include "core/fc_timing.hpp"
#include "core/fc_video.hpp"
#include <array>
using namespace core::fc;
constexpr bool exactNativeRate(unsigned hz) {
  FrameTiming timing(hz);timing.reset(0);int64_t now=0;unsigned drawn=0;
  for(unsigned i=0;i<hz*60;++i) {
    if(timing.draw(now,true)){++drawn;timing.presented(now);}
    const int64_t done=now+4000;
    now=done+timing.finish(done);
  }
  return now==60000000 && drawn<=hz*30 && drawn>=hz*28;
}
constexpr bool lateFramesDoNotDoubleWait() {
  FrameTiming timing(60);timing.reset(0);
  const auto wait=timing.finish(17000);
  return wait==0 && timing.waitTicks(17000,wait,1000)==0;
}
constexpr bool boundedFairnessAndStallRecovery() {
  FrameTiming timing(60);timing.reset(0);
  if(timing.waitTicks(49999,0,1000))return false;
  if(timing.waitTicks(50000,0,1000)!=1)return false;
  if(timing.waitTicks(50001,0,1000))return false;
  const auto wait=timing.finish(500000);
  if(wait<16000||wait>17000)return false;
  timing.reset(9000000);
  return timing.draw(9000000,true)&&timing.finish(9004000)>12000;
}
constexpr bool skipOnlyVideoAndBoundItsAge() {
  FrameTiming timing(60);timing.reset(0);
  if(!timing.draw(0,true))return false;
  timing.presented(0);
  if(timing.draw(0,true))return false; // insufficient render credit
  if(timing.draw(50000,true))return false; // late: skip rendering
  if(!timing.draw(100000,true))return false; // bounded visible refresh
  timing.presented(100000);
  for(unsigned i=0;i<40;++i)if(timing.draw(1000000+i*20000,false))return false;
  return true;
}
constexpr bool nativeViewportIsExact() {
  if(Width!=240 || Height!=240 || CropX!=8 || CropY!=0)return false;
  // Compile the real pixel conversion with padded scanlines and distinguish
  // the first/last visible pixels from the adjacent cropped pixels.
  constexpr unsigned pitch=272,overdraw=8;
  std::array<uint8_t,pitch*SourceHeight> source{};
  std::array<uint16_t,256> palette{};
  std::array<uint16_t,Pixels+2> output{};
  palette[1]=0xF81F;palette[2]=0x07E0;palette[3]=0xFFFF;
  source[overdraw+7]=3;
  source[overdraw+8]=1;
  source[239*pitch+overdraw+247]=2;
  source[239*pitch+overdraw+248]=3;
  output.front()=0xA55A;output.back()=0x5AA5;
  scaleFrame(output.data()+1,source.data(),pitch,overdraw,palette.data());
  if(output.front()!=0xA55A || output.back()!=0x5AA5 ||
     output[1]!=0xF81F || output[Pixels]!=0x07E0)return false;
  for(unsigned i=2;i<Pixels;++i)if(output[i])return false;
  return true;
}
constexpr bool bothPhasesSurvive(unsigned hz) {
  FrameTiming timing(hz);timing.reset(0);int64_t now=0;
  unsigned seen[2]{},last[2]{},attempts=0;
  for(unsigned i=0;i<hz*10;++i) {
    if(timing.draw(now,true)) {
      ++attempts;
      if(attempts%5) { // failed try_lock must not advance the last shown phase
        if(seen[i&1] && i-last[i&1]>16)return false;
        last[i&1]=i;++seen[i&1];timing.presented(now);
      }
    }
    now+=4000;now+=timing.finish(now);
  }
  return attempts<=hz*5+2 && seen[0]>hz && seen[1]>hz;
}
static_assert(exactNativeRate(50));
static_assert(exactNativeRate(60));
static_assert(lateFramesDoNotDoubleWait());
static_assert(boundedFairnessAndStallRecovery());
static_assert(skipOnlyVideoAndBoundItsAge());
static_assert(nativeViewportIsExact());
static_assert(bothPhasesSurvive(50));
static_assert(bothPhasesSurvive(60));
