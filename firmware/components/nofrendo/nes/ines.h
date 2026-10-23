#pragma once
#include <stddef.h>
#include <stdint.h>

// The App and the C engine share this parser. Never approve a header that the
// engine then interprets differently. It is constexpr for host model checks.
#ifdef __cplusplus
#define INES_INLINE static inline constexpr
#else
#define INES_INLINE static inline
#endif
typedef enum {
    INES_OK, INES_HEADER, INES_SIZE, INES_TRUNCATED, INES_CONSOLE,
    INES_MAPPER, INES_SUBMAPPER, INES_RAM, INES_TIMING, INES_LAYOUT
} ines_error_t;
typedef struct {
    size_t offset,prg,chr;
    unsigned mapper,submapper;
    uint8_t pal,nes2,legacy;
} ines_info_t;

INES_INLINE int ines_mapper_supported(unsigned mapper) {
    switch(mapper) {
    case 0: case 1: case 2: case 3: case 4: case 7: case 9: case 10: case 11:
    case 15: case 16: case 18: case 21: case 22: case 23: case 24: case 25:
    case 32: case 33: case 34: case 64: case 65: case 66: case 70: case 71:
    case 73: case 74: case 75: case 78: case 79: case 87: case 93: case 94: case 206: case 245:
        return 1;
    default: return 0;
    }
}
INES_INLINE size_t ines_ram(unsigned shift) {return shift?((size_t)64<<shift):0;}
INES_INLINE ines_error_t ines_parse(const uint8_t *h,size_t size,ines_info_t *out) {
    if(!h || !out || size<16 || h[0]!='N' || h[1]!='E' || h[2]!='S' || h[3]!=0x1a) return INES_HEADER;
    ines_info_t info={0};
    *out=info;
    if(size>2*1024*1024+16+512) return INES_SIZE;
    // A known old dumper put this text over bytes 7..15, including flags.
    const int diskdude=h[7]=='D' && h[8]=='i' && h[9]=='s' && h[10]=='k' &&
        h[11]=='D' && h[12]=='u' && h[13]=='d' && h[14]=='e' && h[15]=='!';
    info.nes2=!diskdude && (h[7]&0x0c)==0x08;
    info.legacy=!info.nes2 && (diskdude || h[12] || h[13] || h[14] || h[15]);
    if(!diskdude && (h[7]&0x0c)!=0 && !info.nes2) return INES_HEADER;
    if(!info.legacy && (h[7]&3)) return INES_CONSOLE;
    info.mapper=(h[6]>>4)|(info.legacy?0:(h[7]&0xf0));
    unsigned prg=h[4],chr=h[5];
    if(info.nes2) {
        info.mapper|=(unsigned)(h[8]&15)<<8;
        info.submapper=h[8]>>4;
    }
    *out=info; // Diagnostics retain the full mapper/submapper on failure.
    if(!ines_mapper_supported(info.mapper)) return INES_MAPPER;
    if(info.submapper) return INES_SUBMAPPER;
    if(info.nes2) {
        // This banked engine needs ordinary 16K PRG/8K CHR counts; reject
        // exponent-sized images and extra hardware instead of misloading them.
        if((h[9]&15)==15 || (h[9]>>4)==15) return INES_LAYOUT;
        prg|=(unsigned)(h[9]&15)<<8;chr|=(unsigned)(h[9]>>4)<<8;
        if(ines_ram(h[10]&15)+ines_ram(h[10]>>4)>8192 ||
           ines_ram(h[11]&15)+ines_ram(h[11]>>4)>8192 ||
           (chr && h[11] && (info.mapper!=74 ||
               ines_ram(h[11]&15)+ines_ram(h[11]>>4)>2048))) return INES_RAM;
        if((h[12]&3)==3 || (h[12]&0xfc)) return INES_TIMING;
        if(h[13] || h[14] || h[15]>1) return INES_LAYOUT;
        info.pal=(h[12]&3)==1; // Multi-region defaults to NTSC.
    } else {
        if(!info.legacy && h[8]>1) return INES_RAM;
        info.pal=!info.legacy && (h[9]&1);
    }
    if(!prg) return INES_HEADER;
    info.offset=16+((h[6]&4)?512:0);
    info.prg=(size_t)prg*16384;info.chr=(size_t)chr*8192;
    if(info.prg+info.chr>2*1024*1024) return INES_SIZE;
    if(info.offset+info.prg+info.chr>size) return INES_TRUNCATED;
    *out=info;return INES_OK;
}
#undef INES_INLINE
