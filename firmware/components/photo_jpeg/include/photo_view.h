#pragma once
#include <stdint.h>
// A window in a logically scaled image. Only width*height pixels are stored.
typedef struct {
    uint32_t scaled_width,scaled_height,x,y;
    uint16_t width,height;
} photo_view;
static inline uint16_t photo_fit_width(uint32_t w,uint32_t h) {
    if(!w || !h) return 0;
    uint32_t fit=w<240?w:240,limit=(uint64_t)w*240/h;
    if(fit>limit)fit=limit;
    return (uint16_t)(fit?fit:1);
}
static inline photo_view photo_view_make(uint32_t w,uint32_t h,uint32_t target,int32_t x,int32_t y) {
    photo_view v={0};if(!w || !h)return v;
    uint32_t fit=photo_fit_width(w,h);
    v.scaled_width=target>fit?target:fit;if(v.scaled_width>w)v.scaled_width=w;
    v.scaled_height=(uint64_t)v.scaled_width*h/w;if(!v.scaled_height)v.scaled_height=1;
    if(v.scaled_width==fit && v.scaled_height>240)v.scaled_height=240;
    v.width=(uint16_t)(v.scaled_width<240?v.scaled_width:240);
    v.height=(uint16_t)(v.scaled_height<240?v.scaled_height:240);
    v.x=x<0?0:(uint32_t)x;v.y=y<0?0:(uint32_t)y;
    if(v.x>v.scaled_width-v.width)v.x=v.scaled_width-v.width;
    if(v.y>v.scaled_height-v.height)v.y=v.scaled_height-v.height;
    return v;
}
static inline uint32_t photo_sample(uint32_t position,uint32_t source,uint32_t scaled) {
    return (uint32_t)(((uint64_t)position*2+1)*source/((uint64_t)scaled*2));
}
static inline int photo_view_valid(const photo_view *v,uint32_t w,uint32_t h) {
    return v && v->width && v->height && v->width<=240 && v->height<=240 &&
        v->scaled_width && v->scaled_height && v->scaled_width<=w && v->scaled_height<=h &&
        v->width<=v->scaled_width && v->height<=v->scaled_height &&
        v->x<=v->scaled_width-v->width && v->y<=v->scaled_height-v->height;
}
