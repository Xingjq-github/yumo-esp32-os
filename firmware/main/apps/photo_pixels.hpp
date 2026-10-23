#pragma once
#include <cstddef>
#include <cstdint>

namespace apps::photo_pixels {
constexpr size_t MaxPixels=1024*1024; // Same 2 MiB RGB565 image limit as before.
constexpr size_t Alignment=16;

constexpr size_t rgb888Bytes(uint16_t width,uint16_t height)
{
    const size_t pixels=size_t(width)*height;
    return pixels && pixels<=MaxPixels?pixels*3:0;
}

// Ordered dithering is anchored to image coordinates, so panning cannot make
// gradients flicker. Quantize each full-range RGB channel only once. Matching
// thresholds keep neutral tones together; exact black/white/primaries stay exact.
constexpr uint8_t Bayer4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
constexpr uint8_t quantize(uint8_t value,unsigned levels,unsigned threshold)
{
    const unsigned scaled=unsigned(value)*levels;
    return uint8_t(scaled/255+((scaled%255)*32>(2*threshold+1)*255?1:0));
}
constexpr uint16_t rgb565(uint8_t red,uint8_t green,uint8_t blue,unsigned x,unsigned y)
{
    const unsigned threshold=Bayer4[(y%4)*4+x%4];
    return uint16_t((unsigned(quantize(red,31,threshold))<<11) |
                    (unsigned(quantize(green,63,threshold))<<5) |
                     unsigned(quantize(blue,31,threshold)));
}

// esp_new_jpeg RGB888 is R,G,B in ascending addresses. LVGL RGB565 on this
// little-endian CPU is low byte, high byte. The display adapter swaps once for
// SPI; doing another swap here would corrupt colors. Compact in place to avoid
// simultaneously allocating full RGB888 and RGB565 frames.
constexpr bool rgb888To565InPlace(uint8_t *pixels,uint16_t width,uint16_t height,size_t available,unsigned offsetX=0,unsigned offsetY=0)
{
    const size_t bytes=rgb888Bytes(width,height);
    if(!pixels || !bytes || available<bytes) return false;
    for(unsigned y=0;y<height;++y) {
        for(unsigned x=0;x<width;++x) {
            const size_t index=size_t(y)*width+x;
            const uint16_t color=rgb565(pixels[index*3],pixels[index*3+1],pixels[index*3+2],x+offsetX,y+offsetY);
            pixels[index*2]=uint8_t(color);
            pixels[index*2+1]=uint8_t(color>>8);
        }
    }
    return true;
}
} // namespace apps::photo_pixels
