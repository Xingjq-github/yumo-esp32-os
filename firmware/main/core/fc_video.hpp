#pragma once
#include <cstdint>
namespace core::fc {
inline constexpr unsigned SourceWidth=256,SourceHeight=240;
inline constexpr unsigned Width=240,Height=240,Pixels=Width*Height;
inline constexpr unsigned CropX=(SourceWidth-Width)/2,CropY=(SourceHeight-Height)/2;
static_assert(Width<=SourceWidth && Height<=SourceHeight);
static_assert(CropX*2+Width==SourceWidth && CropY*2+Height==SourceHeight);
// Keep the shared live-frame / restored-preview entry point. Pixels now map
// 1:1: retain source x=8..247 and every row, centered on the 240x240 display.
// The PPU's overdraw padding is separate from the visible-image crop.
// One palette lookup per pixel; no interpolation or coordinate tables.
constexpr void scaleFrame(uint16_t *out,const uint8_t *source,unsigned pitch,
                          unsigned overdraw,const uint16_t *colors) {
  for(unsigned y=0;y<Height;++y) {
    const auto line=source+(y+CropY)*pitch+overdraw+CropX;
    for(unsigned x=0;x<Width;++x)*out++=colors[line[x]];
  }
}
} // namespace core::fc
