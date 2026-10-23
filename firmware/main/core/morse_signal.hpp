#pragma once
#include <cstdint>
#include <string_view>
namespace core::morse_audio {
// Encoded command: nonzero length in the high byte, dash bits low-to-high.
constexpr uint32_t encode(std::string_view text) {
 if(text.empty()||text.size()>8)return 0;
 uint32_t bits=0;for(unsigned i=0;i<text.size();++i){if(text[i]=='-')bits|=1u<<i;else if(text[i]!='.')return 0;}
 return (uint32_t(text.size())<<8)|bits;
}
struct Signal {
 static constexpr unsigned Rate=16000,Unit=Rate*120/1000;
 uint32_t phase=0,remaining=0,pattern=0;
 unsigned index=0,length=0,envelope=0;bool tone=false,gap=false;
 void clear(){remaining=pattern=index=length=0;tone=gap=false;}
 void play(uint32_t command){clear();length=(command>>8)&15;pattern=command&255;if(length>8)length=0;}
 bool active()const{return length||envelope;}
 int16_t sample(bool key) {
  if(length&&!remaining) {
   if(gap){remaining=Unit;tone=false;gap=false;}
   else if(index<length){remaining=Unit*((pattern&(1u<<index))?3:1);++index;tone=true;gap=true;}
   else {length=0;tone=false;}
  }
  const bool on=key||(length&&tone);
  if(on){if(envelope<80)++envelope;}else if(envelope)--envelope;
  // 700 Hz sine, 5 ms click-suppressing envelope; fixed 64-byte waveform.
  static constexpr int16_t sine[32]={0,6393,12539,18204,23170,27245,30273,32137,32767,32137,30273,27245,23170,18204,12539,6393,0,-6393,-12539,-18204,-23170,-27245,-30273,-32137,-32767,-32137,-30273,-27245,-23170,-18204,-12539,-6393};
  phase+=uint32_t((uint64_t(700)<<32)/Rate);
  const int16_t out=int32_t(sine[phase>>27])*int(envelope)/(80*8);
  if(remaining)--remaining;
  return out;
 }
};
}
