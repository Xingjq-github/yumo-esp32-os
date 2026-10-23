#include "apps/photo_pixels.hpp"
#include <array>
namespace p=apps::photo_pixels;

constexpr bool primaryColorsAndByteOrder()
{
    // Odd-width, multirow RGB source; the trailing sentinel must survive.
    std::array<uint8_t,19> frame={255,0,0, 0,255,0, 0,0,255,
                                0,0,0, 255,255,255, 255,0,255, 0xa5};
    if(!p::rgb888To565InPlace(frame.data(),3,2,18)) return false;
    constexpr uint16_t colors[]={0xf800,0x07e0,0x001f,0x0000,0xffff,0xf81f};
    for(unsigned i=0;i<6;++i)
        if(frame[i*2]!=uint8_t(colors[i]) || frame[i*2+1]!=uint8_t(colors[i]>>8)) return false;
    return frame[18]==0xa5;
}
constexpr bool gradientsKeepAverageAndChannelRange()
{
    for(unsigned levels:{31u,63u}) {
        for(unsigned value=0;value<=255;++value) {
            int total=0;
            for(unsigned phase=0;phase<16;++phase) {
                const unsigned q=p::quantize(uint8_t(value),levels,phase);
                if(q>levels || (value && q<p::quantize(uint8_t(value-1),levels,phase))) return false;
                total+=q;
            }
            // The 4x4 spatial average stays within half of one accumulated code.
            const int error=total*255-int(value*levels*16);
            if(error < -128 || error > 128) return false;
        }
    }
    for(unsigned x=0;x<4;++x) for(unsigned y=0;y<4;++y) {
        if(p::rgb565(0,0,0,x,y)!=0 || p::rgb565(255,255,255,x,y)!=0xffff) return false;
        // Different channels must never be swapped or use the wrong bit depth.
        if(p::rgb565(255,0,0,x,y)!=0xf800 || p::rgb565(0,255,0,x,y)!=0x07e0 ||
           p::rgb565(0,0,255,x,y)!=0x001f) return false;
    }
    return true;
}
constexpr bool rejectsInvalidBuffersWithoutWriting()
{
    uint8_t bytes[6]={7,8,9,10,11,12};
    if(p::rgb888To565InPlace(bytes,2,1,5) || p::rgb888To565InPlace(bytes,0,1,6) ||
       p::rgb888To565InPlace(nullptr,1,1,3)) return false;
    for(unsigned i=0;i<6;++i) if(bytes[i]!=7+i) return false;
    return true;
}
static_assert(primaryColorsAndByteOrder(),"RGB bytes must become native LVGL RGB565, with no row shift");
static_assert(gradientsKeepAverageAndChannelRange(),"Dithering must preserve average color, endpoints and channel range");
static_assert(rejectsInvalidBuffersWithoutWriting(),"Invalid buffers must leave source data untouched");
static_assert(p::rgb888Bytes(1024,1024)==3*1024*1024);
static_assert(p::rgb888Bytes(1025,1024)==0);
static_assert(p::rgb888Bytes(65535,65535)==0);
