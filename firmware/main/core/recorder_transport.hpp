#pragma once
#include <cstdint>
#include "wav_format.hpp"
namespace core::recorder {
// Only reverse preview may change pitch. Filter before 2:1 decimation to avoid
// aliasing; the caller supplies samples in reverse chronological order.
class RewindSpeed {
 int16_t history[7]{};unsigned cursor=0,phase=0;
public:
 constexpr void reset(){for(auto &v:history)v=0;cursor=phase=0;}
 constexpr unsigned process(int16_t *samples,unsigned count) {
  unsigned out=0;
  constexpr int weights[7]={-1024,0,9216,16384,9216,0,-1024};
  for(unsigned i=0;i<count;++i) {
   history[cursor]=samples[i];cursor=(cursor+1)%7;
   if(phase++%2==0)continue;
   int32_t mixed=0;for(unsigned k=0;k<7;++k)mixed+=int32_t(history[(cursor+k)%7])*weights[k];
   const int value=mixed/32768;
   samples[out++]=int16_t(value<-32768?-32768:value>32767?32767:value);
  }
  return out;
 }
};
constexpr uint32_t rewindBlockBytes(uint32_t position) {
 return (position>2048?2048:position)&~1U;
}
} // namespace core::recorder
