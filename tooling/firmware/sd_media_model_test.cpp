// Compile-only tests of the actual production models, not reimplementations.
#include "core/fc_model.hpp"
#include "core/wav_format.hpp"
using namespace core;
constexpr bool keyModel() {
  fc::Mapping m;
  sys::hidinput::Keys keys;
  for(unsigned i=0;i<8;++i) {
    keys={};keys.set(m.usage[i]);
    if(m.pad(keys)!=(1u<<i)) return false;
  }
  keys={}; keys.set(m.usage[fc::Up]); keys.set(m.usage[fc::Down]);
  if(m.pad(keys)) return false;
  keys={}; keys.set(m.usage[fc::Left]); keys.set(m.usage[fc::Right]);
  if(m.pad(keys)) return false;
  keys={}; keys.set(m.usage[fc::A]);keys.set(m.usage[fc::Right]);
  if(m.pad(keys)!=0x81) return false;
  keys={};keys.set(m.usage[fc::Pause]);keys.set(m.usage[fc::Exit]);
  if(m.pad(keys)) return false;
  m.usage[fc::A]=fc::keyUsage("K");keys={};keys.set(fc::keyUsage("K"));
  return m.pad(keys)==1;
}
static_assert(keyModel());
static_assert(fc::keyUsage("z")==0x1d && fc::keyUsage("X")==0x1b);
static_assert(fc::keyUsage("0")==0x27 && fc::keyUsage("1")==0x1e);
static_assert(fc::keyUsage("f1")==0x3a && fc::keyUsage("F12")==0x45);
static_assert(fc::keyUsage("Escape")==0x29 && fc::keyUsage("ESC")==0x29);
static_assert(fc::keyUsage("Enter")==0x28 && fc::keyUsage("Tab")==0x2b);
static_assert(fc::keyUsage("SPACE")==0x2c && fc::keyUsage("Backspace")==0x2a);
static_assert(fc::keyUsage("F13")==0 && fc::keyUsage("F0")==0 && fc::keyUsage("F999999999999")==0);
static_assert(fc::keyUsage("")==0 && fc::keyUsage("Ctrl+Z")==0 && fc::keyUsage("Unknown")==0);
constexpr bool romModel() {
  std::array<uint8_t,16> h{'N','E','S',0x1a,1,1};
  unsigned mapper=99;constexpr size_t size=16+16384+8192;
  if(!fc::headerValid(h.data(),size,mapper) || mapper!=0) return false;
  if(fc::headerValid(nullptr,size,mapper) || fc::headerValid(h.data(),15,mapper)) return false;
  if(fc::headerValid(h.data(),size-1,mapper) || fc::headerValid(h.data(),2*1024*1024+529,mapper)) return false;
  for(unsigned m=0;m<256;++m) {
    h[6]=(m&15)<<4;h[7]=m&0xf0;
    if(fc::headerValid(h.data(),size,mapper)!=fc::supported(m)) return false;
  }
  h[6]=4;h[7]=0;
  if(fc::headerValid(h.data(),size,mapper) || !fc::headerValid(h.data(),size+512,mapper)) return false;
  h[6]=0;h[7]=8; // Basic NES2 uses the same parser in C and C++.
  if(!fc::headerValid(h.data(),size,mapper)) return false;
  h[8]=0x10;
  if(fc::headerValid(h.data(),size,mapper)) return false;
  h[8]=0;
  h[7]=1; // VS arcade is not standard NES
  if(fc::headerValid(h.data(),size,mapper)) return false;
  h[7]=0;h[4]=0;
  if(fc::headerValid(h.data(),size,mapper)) return false;
  h[4]=1;h[5]=0;
  if(!fc::headerValid(h.data(),16+16384,mapper)) return false;
  h[0]='X';return !fc::headerValid(h.data(),size,mapper);
}
static_assert(romModel());
constexpr bool extendedRomModel() {
  std::array<uint8_t,16> h{'N','E','S',0x1a,1,1};
  constexpr size_t bytes=16+16384+8192;
  ines_info_t info{};
  // VRC4 and the same cart in the supported NES2 subset.
  h[6]=0x50;h[7]=0x10;
  if(ines_parse(h.data(),bytes,&info)!=INES_OK || info.mapper!=21) return false;
  h[7]=0x18;h[12]=1;
  if(ines_parse(h.data(),bytes,&info)!=INES_OK || !info.nes2 || !info.pal) return false;
  h[12]=3;
  if(ines_parse(h.data(),bytes,&info)!=INES_TIMING) return false;
  h[12]=0;h[8]=0x20;
  if(ines_parse(h.data(),bytes,&info)!=INES_SUBMAPPER || info.submapper!=2) return false;
  h[8]=1;
  if(ines_parse(h.data(),bytes,&info)!=INES_MAPPER || info.mapper!=277) return false;
  h[8]=0;h[10]=8; // 16K RAM is beyond this engine's supported board layout.
  if(ines_parse(h.data(),bytes,&info)!=INES_RAM) return false;
  h[10]=7;h[6]|=4;
  if(ines_parse(h.data(),bytes,&info)!=INES_TRUNCATED) return false;
  if(ines_parse(h.data(),bytes+512,&info)!=INES_OK || info.offset!=528) return false;
  h={'N','E','S',0x1a,1,1};
  constexpr char garbage[]="DiskDude!";
  for(unsigned i=0;i<9;++i) h[7+i]=garbage[i];
  if(ines_parse(h.data(),bytes,&info)!=INES_OK || info.mapper || !info.legacy || info.pal) return false;
  h={'N','E','S',0x1a,128,0}; // 2MB payload plus header is valid.
  if(ines_parse(h.data(),16+2*1024*1024,&info)!=INES_OK) return false;
  h[5]=1;
  return ines_parse(h.data(),16+2*1024*1024,&info)==INES_SIZE;
}
static_assert(extendedRomModel());
constexpr bool wavModel() {
  for(uint32_t size: std::array<uint32_t,5>{0,1024,wav::ByteRate,1920000,wav::MaxBytes}) {
    const auto h=wav::header(size);uint32_t out=123;
    if(!wav::parse(h.data(),uint64_t(size)+44,out) || out!=size) return false;
    if(wav::parse(h.data(),uint64_t(size)+43,out)) return false;
  }
  auto h=wav::header(1024);uint32_t out=0;
  if(wav::parse(nullptr,1068,out)) return false;
  for(unsigned i: {0U,8U,12U,16U,20U,21U,22U,23U,24U,28U,32U,33U,34U,35U,36U}) {
    auto invalid=h;invalid[i]^=0xff;
    if(wav::parse(invalid.data(),1068,out)) return false;
  }
  h=wav::header(1025);
  if(wav::parse(h.data(),1069,out)) return false;
  h=wav::header(wav::MaxBytes+2);
  if(wav::parse(h.data(),uint64_t(wav::MaxBytes)+46,out)) return false;
  h=wav::header(2048);h[4]^=1;
  if(wav::parse(h.data(),2092,out)) return false;
  // A pause writes no samples. Resume accumulates the same data chunk.
  h=wav::header(1024+0+1024);
  return wav::parse(h.data(),2092,out) && out==2048;
}
static_assert(wavModel());
static_assert(wav::ByteRate*60==1920000);
