// Compile-time DSP tests run the real recording processor on synthetic PCM.
#include "core/recording_level.hpp"
#include <array>
using Level = core::recorder::RecordingLevel;
constexpr unsigned amplitude(int16_t sample) {
  return sample < 0 ? unsigned(-int(sample)) : unsigned(sample);
}
constexpr void tone(std::array<int16_t,512> &data, int size, int bias=0) {
  for(size_t i=0;i<data.size();++i)
    data[i]=static_cast<int16_t>(bias+((i%32)<16?size:-size));
}
constexpr bool silenceAndInvalidBuffers() {
  Level level;std::array<int16_t,512> data{};
  if(level.process(nullptr,512)!=0 || level.process(data.data(),0)!=0 ||
     level.process(data.data(),513)!=0) return false;
  for(unsigned n=0;n<20;++n) {
    if(level.process(data.data(),data.size())!=0 || level.gainQ12()!=Level::BaseGain) return false;
  }
  for(auto sample:data) if(sample!=0) return false;
  return true;
}
constexpr bool lowSpeechBoost() {
  Level level;std::array<int16_t,512> data{};
  unsigned first=0,last=0;
  for(unsigned n=0;n<24;++n) {
    tone(data,600);
    last=level.process(data.data(),data.size());
    if(!n)first=last;
    if(last>Level::Ceiling || level.gainQ12()>Level::MaxGain)return false;
  }
  return last>3000 && last>first && level.gainQ12()>4*Level::One;
}
constexpr bool loudStepAndNoWrap() {
  Level level;std::array<int16_t,512> data{};
  for(unsigned n=0;n<12;++n) {tone(data,600);level.process(data.data(),data.size());}
  for(size_t i=0;i<data.size();++i)data[i]=(i%2)?-32768:32767;
  const auto peak=level.process(data.data(),data.size());
  for(size_t i=0;i<data.size();++i) {
    if(amplitude(data[i])>Level::Ceiling)return false;
    if((i%2 && data[i]>=0) || (!(i%2) && data[i]<=0))return false;
  }
  return peak>=29000 && peak<=Level::Ceiling && level.gainQ12()==Level::One;
}
constexpr bool quietNoiseDoesNotPump() {
  Level level;std::array<int16_t,512> data{};
  for(unsigned n=0;n<24;++n) {
    tone(data,30);
    if(level.process(data.data(),data.size())>100 || level.gainQ12()!=Level::BaseGain)return false;
  }
  return true;
}
constexpr bool dcBiasRemoved() {
  Level level;std::array<int16_t,512> data{};unsigned peak=0;
  for(unsigned n=0;n<6;++n) {
    for(auto &sample:data)sample=8000;
    peak=level.process(data.data(),data.size());
  }
  return peak<10;
}
constexpr bool resetForResumeAndNewFile() {
  Level a,b;std::array<int16_t,512> left{},right{};
  tone(left,600,3000);a.process(left.data(),left.size());a.reset();
  tone(left,1600);right=left;
  a.process(left.data(),left.size());b.process(right.data(),right.size());
  return left==right && a.gainQ12()==b.gainQ12();
}
constexpr bool sustainedLoudStaysBounded() {
  Level level;std::array<int16_t,512> data{};
  for(unsigned n=0;n<12;++n) {
    tone(data,16000,12000);
    const auto peak=level.process(data.data(),data.size());
    if(peak>Level::Ceiling)return false;
    for(auto sample:data)if(amplitude(sample)>Level::Ceiling)return false;
  }
  return true;
}
static_assert(silenceAndInvalidBuffers());
static_assert(lowSpeechBoost());
static_assert(loudStepAndNoWrap());
static_assert(quietNoiseDoesNotPump());
static_assert(dcBiasRemoved());
static_assert(resetForResumeAndNewFile());
static_assert(sustainedLoudStaysBounded());
static_assert(sizeof(Level)<=16);
