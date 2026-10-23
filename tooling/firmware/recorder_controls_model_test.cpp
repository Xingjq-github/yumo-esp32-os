#include "ui/recorder_layout.hpp"
#include "core/recorder_transport.hpp"
#include "core/recording_latency.hpp"
#include "core/dual_mic_input.hpp"
#include "core/recording_level.hpp"
#include <array>
namespace l=ui::recorder_layout;
using namespace core::recorder;
constexpr bool layoutFits() {
 constexpr l::Rect all[]={l::Record,l::Stop,l::Rewind,l::Pause,l::Forward,l::List};
 for(unsigned i=0;i<6;++i) {
  if(!l::inside(all[i]))return false;
  for(unsigned j=i+1;j<6;++j)if(l::overlaps(all[i],all[j]))return false;
 }
 for(unsigned i=0;i<5;++i)if(all[i].w<36||all[i].h<36)return false;
 return l::Record.w==118&&l::Record.h==77&&l::Stop.h==40&&l::Stop.w==121&&
        l::Record.y==l::Stop.y&&l::Record.y+l::Record.h==l::Forward.y+l::Forward.h&&
        l::List.y==l::Forward.y+l::Forward.h+1&&l::List.h==98&&l::MeterGap==5&&l::List.h>84&&l::List.y<156&&l::DeleteWidth>=36&&l::RowHeight>=36;
}
constexpr bool controlsLocked() {
 for(bool ready:{false,true})for(bool ending:{false,true}) {
  auto p=l::controls(true,false,ready,ending,false);
  if(p.record!=!ending||p.stop!=!ending||!p.navigate||p.files||p.remove!=!ending||p.transport!=(ready&&!ending))return false;
  if(l::canDelete(p,true,false)||l::canDelete(p,true,true)!=!ending)return false;
 }
 auto r=l::controls(true,true,true,false,false);
 if(!r.record||!r.stop||r.transport||!r.navigate||r.files||r.remove)return false;
 auto idle=l::controls(false,false,false,false,false);
 if(!idle.record||idle.stop||idle.transport||!idle.files||!idle.navigate||!l::canDelete(idle,false,false))return false;
 auto blocked=l::controls(false,false,false,false,true);
 return !blocked.record&&!blocked.stop&&!blocked.transport&&!blocked.navigate&&!blocked.files&&!blocked.remove;
}
constexpr bool seekingIsTwoTimesAndBounded() {
 for(uint32_t position:{0U,1U,2U,100U,2046U,2048U,4096U,0xffff0000U}) {
  const auto bytes=rewindBlockBytes(position);
  if(bytes>position||bytes>2048||(bytes&1))return false;
  RewindSpeed reverse;std::array<int16_t,1024> samples{};
  if(reverse.process(samples.data(),bytes/2)!=bytes/4)return false;
 }
 return rewindBlockBytes(2048)==2048;
}
constexpr bool latencyPreservesSamplesAndPause() {
 RecordingLatency delay;unsigned input=0,output=0;
 if(delay.flush())return false;
 for(unsigned segment=1;segment<5;++segment) {
  delay.reset();
  for(unsigned frame=0;frame<segment*2;++frame) {
   input+=256;if(delay.acceptFrame())output+=256;
  }
  if(delay.flush())output+=256;
  if(delay.flush()||output!=input)return false;
 }
 return input==output;
}
constexpr bool coherentPairReducesOpposedNoise() {
 DualMicInput mixed,reference,single;
 std::array<int16_t,1024>a{},b{},c{};
 uint64_t mixedError=0,singleError=0;
 for(unsigned block=0;block<12;++block) {
  for(unsigned i=0;i<512;++i) {
   int signal=(i%32<16?1200:-1200),noise=((i*13)%17<8?200:-200);
   a[2*i]=signal+noise;a[2*i+1]=signal-noise;
   b[2*i]=b[2*i+1]=signal;
   c[2*i]=signal+noise;c[2*i+1]=0;
  }
  mixed.process(a.data(),512);reference.process(b.data(),512);single.process(c.data(),512);
  if(block>=8)for(unsigned i=0;i<512;++i) {
   const int ma=int(a[i])-b[i],sa=int(c[i])-b[i];
   mixedError+=ma*ma;singleError+=sa*sa;
  }
 }
 return mixedError*4<singleError;
}
constexpr bool speechGateDoesNotDeleteSamplesOrBoostNoise() {
 RecordingLevel gain;std::array<int16_t,512> pcm{};
 for(unsigned n=0;n<12;++n) {
  for(unsigned i=0;i<512;++i)pcm[i]=(i%2)?600:-600;
  gain.processDcBlocked(pcm.data(),512,false);
  if(gain.gainQ12()!=RecordingLevel::BaseGain||pcm.back()==0)return false;
 }
 return true;
}
static_assert(layoutFits());
static_assert(controlsLocked());
static_assert(seekingIsTwoTimesAndBounded());
static_assert(latencyPreservesSamplesAndPause());
static_assert(coherentPairReducesOpposedNoise());
static_assert(speechGateDoesNotDeleteSamplesOrBoostNoise());
