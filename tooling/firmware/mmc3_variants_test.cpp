#include "../../firmware/components/nofrendo/nes/ines.h"

constexpr bool mixedChrHeader() {
    uint8_t h[16]={'N','E','S',0x1a,32,32,0xa0,0x48,0,0,7,5};
    constexpr size_t size=16+512*1024+256*1024;
    ines_info_t info{};
    if(ines_parse(h,size,&info)!=INES_OK || info.mapper!=74) return false;
    h[11]=6;
    if(ines_parse(h,size,&info)!=INES_RAM) return false;
    h[11]=5;h[6]=0x40;h[7]=8;
    if(ines_parse(h,size,&info)!=INES_RAM) return false;
    h[6]=0xa0;h[7]=0x48;h[8]=0x10;
    return ines_parse(h,size,&info)==INES_SUBMAPPER;
}
constexpr bool largePrgHeader() {
    uint8_t h[16]={'N','E','S',0x1a,64,0,0x50,0xf8,0,0,7,7};
    constexpr size_t size=16+1024*1024;
    ines_info_t info{};
    if(ines_parse(h,size,&info)!=INES_OK || info.mapper!=245 || info.prg!=1024*1024) return false;
    h[11]=8;
    if(ines_parse(h,size,&info)!=INES_RAM) return false;
    h[11]=7;h[5]=1;
    return ines_parse(h,size+8192,&info)==INES_RAM;
}
static_assert(mixedChrHeader());
static_assert(largePrgHeader());
static_assert(ines_mapper_supported(74) && ines_mapper_supported(245));
static_assert(!ines_mapper_supported(119) && !ines_mapper_supported(176));
