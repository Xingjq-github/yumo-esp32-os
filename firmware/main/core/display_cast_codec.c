#include "display_cast_codec.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"
#endif
static void put16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {put16(p,(uint16_t)n);put16(p+2,(uint16_t)(n>>16));}
static uint16_t get16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t get32(const uint8_t *p) {return get16(p)|((uint32_t)get16(p+2)<<16);}
uint32_t ymcast_crc32(const uint8_t *data,size_t length) {
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(0,data,(uint32_t)length);
#else
    uint32_t crc=~(uint32_t)0;
    for(size_t i=0;i<length;++i) {crc^=data[i];for(int b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u& (0u-(crc&1u)));}
    return ~crc;
#endif
}
void ymcast_header(uint8_t out[YMCAST_HEADER],const ymcast_packet *p) {
    memset(out,0,YMCAST_HEADER);memcpy(out,"YUMOCAST",8);
    out[8]=1;out[9]=p->kind;out[10]=p->format;out[11]=p->flags;
    put32(out+12,p->session);put32(out+16,p->sequence);
    if(p->kind==YMCAST_FRAME) {put16(out+20,YMCAST_WIDTH);put16(out+22,YMCAST_HEIGHT);}
    put32(out+24,p->length);put32(out+28,p->crc);put32(out+32,ymcast_crc32(out,32));
}
int ymcast_parse(const uint8_t h[YMCAST_HEADER],ymcast_packet *p) {
    if(memcmp(h,"YUMOCAST",8) || h[8]!=1 || ymcast_crc32(h,32)!=get32(h+32))return 0;
    p->kind=h[9];p->format=h[10];p->flags=h[11];p->session=get32(h+12);
    p->sequence=get32(h+16);p->length=get32(h+24);p->crc=get32(h+28);
    if(p->flags>1)return 0;
    if(p->kind==YMCAST_FRAME) {
        return p->session && p->sequence && p->flags==1 && p->format<=YMCAST_RLE &&
            get16(h+20)==YMCAST_WIDTH && get16(h+22)==YMCAST_HEIGHT && p->length>0 &&
            p->length<=YMCAST_BYTES && (p->format!=YMCAST_RAW || p->length==YMCAST_BYTES);
    }
    if(p->kind!=YMCAST_STATUS && p->kind!=YMCAST_END &&
       (p->kind<YMCAST_HELLO || p->kind>YMCAST_STOP))return 0;
    return !p->length && !p->crc && !p->format && !get16(h+20) && !get16(h+22);
}
static int same(const uint8_t *p,size_t a,size_t b) {return p[2*a]==p[2*b] && p[2*a+1]==p[2*b+1];}
size_t ymcast_encode(const uint8_t *p,size_t count,uint8_t *out,size_t cap) {
    if(!p || !out || count>YMCAST_PIXELS)return 0;
    size_t i=0,n=0;
    while(i<count) {
        size_t run=1;
        while(run<128 && i+run<count && same(p,i,i+run))++run;
        if(run>=3) {
            if(n+3>cap || n+3>=count*2)return 0;
            out[n++]=(uint8_t)(0x80|(run-1));out[n++]=p[i*2];out[n++]=p[i*2+1];i+=run;
        } else {
            size_t start=i;i+=run;
            while(i<count && i-start<128) {
                if(i+2<count && same(p,i,i+1) && same(p,i,i+2))break;
                ++i;
            }
            size_t length=i-start;
            if(n+1+length*2>cap || n+1+length*2>=count*2)return 0;
            out[n++]=(uint8_t)(length-1);memcpy(out+n,p+start*2,length*2);n+=length*2;
        }
    }
    return n;
}
int ymcast_decode(const uint8_t *data,size_t length,uint8_t *out,size_t pixels) {
    if(!data || !out || pixels>YMCAST_PIXELS)return 0;
    size_t i=0,n=0;
    while(i<length) {
        uint8_t tag=data[i++];size_t count=(tag&127)+1;
        if(n+count>pixels)return 0;
        if(tag&128) {
            if(i+2>length)return 0;
            for(size_t j=0;j<count;++j) {out[(n+j)*2]=data[i];out[(n+j)*2+1]=data[i+1];}
            i+=2;
        } else {
            if(i+count*2>length)return 0;
            memcpy(out+n*2,data+i,count*2);i+=count*2;
        }
        n+=count;
    }
    return n==pixels;
}
int ymcast_copy_rect(uint8_t *canvas,int x1,int y1,int x2,int y2,
                     const uint8_t *source,size_t stride,size_t source_size) {
    if(!canvas || !source || x1<0 || y1<0 || x2<x1 || y2<y1 ||
       x2>=YMCAST_WIDTH || y2>=YMCAST_HEIGHT)return 0;
    size_t width=(size_t)(x2-x1+1)*2,height=(size_t)(y2-y1+1);
    if(stride<width || stride>YMCAST_BYTES || (height-1)*stride+width>source_size)return 0;
    for(size_t row=0;row<height;++row)
        memcpy(canvas+((y1+row)*YMCAST_WIDTH+x1)*2,source+row*stride,width);
    return 1;
}
