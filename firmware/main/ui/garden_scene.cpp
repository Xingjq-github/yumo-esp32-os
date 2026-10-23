#include "garden_scene.hpp"
#include "garden_art.hpp"
#include <algorithm>
namespace ui::garden_scene {
namespace {
uint16_t rgb(uint32_t v){return uint16_t(((v>>8)&0xf800)|((v>>5)&0x7e0)|((v>>3)&0x1f));}
struct Paint {
    Pixels &p;
    void dot(int x,int y,uint32_t c){if(x>=0 && y>=0 && x<int(Width) && y<int(Height))p[y*Width+x]=rgb(c);}
    void box(int x,int y,int w,int h,uint32_t c){for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)dot(xx,yy,c);}
    void oval(int x,int y,int rx,int ry,uint32_t c){for(int yy=-ry;yy<=ry;++yy)for(int xx=-rx;xx<=rx;++xx)if(xx*xx*ry*ry+yy*yy*rx*rx<=rx*rx*ry*ry)dot(x+xx,y+yy,c);}
    void star(int x,int y,uint32_t c){box(x-2,y,5,1,c);box(x,y-2,1,5,c);dot(x,y,0xFFF9DC);}
    void heart(int x,int y){box(x,y,3,2,0xF29BAD);box(x+4,y,3,2,0xF29BAD);box(x-1,y+2,9,2,0xF29BAD);box(x,y+4,7,2,0xF29BAD);box(x+2,y+6,3,1,0xF29BAD);dot(x+3,y+7,0xF29BAD);box(x,y+1,2,1,0xFFE7D4);}
    void tree(int x,int y,int scale=1){
        box(x-2,y,5,22,0x8D785C);box(x-1,y+3,2,17,0xB0956F);
        oval(x,y-3,16*scale,14*scale,0x507D64);oval(x-4,y-7,13*scale,10*scale,0x79A27B);
        oval(x+5,y-11,9*scale,8*scale,0xAAC58D);box(x-9,y-12,5,2,0xD0DC9B);
    }
};
}
void render(Pixels &pixels,const core::garden::State &s,unsigned frame,const View &view){
    Paint p{pixels};const bool night=s.weather==2;const int biome=view.biome<0?s.biome:view.biome;
    p.box(0,0,120,120,night?0x344968:s.weather==1?0xBED1CA:0xD3E7C7);
    p.oval(15,63,42,27,night?0x526E80:0xA2C4A4);p.oval(99,56,43,25,night?0x668087:0xB1CEAA);
    p.oval(57,76,72,27,night?0x708C83:0x8EB18C);
    p.box(0,72,120,48,biome==1?0xDCC9A0:night?0x819B84:0xB7C993);
    p.oval(61,99,35,32,biome==1?0xECDCB4:night?0xA4AD8A:0xD3D4A0);
    p.oval(58,94,29,25,biome==1?0xF2E3C0:night?0xAFB796:0xDDDCAE);
    if(night){
        p.oval(91,28,7,7,0xF6E7B4);p.oval(94,25,6,6,0x344968);
        for(int n=0;n<12;++n)p.star((n*31+7)%119,23+(n*13)%37,(frame+n)%8?0xB9D8D2:0xFFF1BE);
    }else {
        p.oval(90,28,7,7,0xF6D988);p.oval(28,28,13,4,0xF6FAE2);p.oval(18,31,12,3,0xF6FAE2);
        p.oval(73,17,10,3,0xF6FAE2);
    }
    if(biome==0){
        p.tree(8,46);p.tree(116,43);p.oval(15,78,20,9,0x71966C);p.oval(107,82,18,10,0x88A67A);
        p.box(8,84,13,4,0xAD825D);p.oval(14,82,9,3,0xD18F7E);p.box(8,79,5,2,0xF7DEAA);
        p.box(102,79,4,7,0xD2C69A);p.oval(104,78,7,3,0xD49A82);p.dot(101,77,0xFFF0C9);
    }else if(biome==1){
        p.oval(126,77,32,40,0x7FB5AD);p.oval(127,84,29,39,0xA3CBC0);p.oval(128,93,25,39,0xC6DFCC);
        for(int n=0;n<4;++n)p.box(99+(n*11+frame/2)%17,65+n*10,8,1,0xE8EFCC);
        p.box(10,48,4,29,0x9D8360);p.oval(12,46,18,4,0x73997B);p.oval(6,42,12,3,0xA8BD8A);
        p.star(22,88,0xECA991);p.oval(99,105,3,2,0xEBAF9A);
    }else {
        p.oval(8,74,15,8,0x93A597);p.oval(8,71,12,6,0xC4C7A8);p.oval(6,70,6,3,0xDBD8B8);
        p.box(108,40,2,35,0x8C957C);p.box(101,43,16,2,0xB9A478);
        for(int n=0;n<3;++n){p.box(101+n*7,44,1,9+n*4,0x819A89);p.star(101+n*7,54+n*4,0xE8CC85);}
        p.oval(10,103,23,7,0xE8EBCF);p.oval(99,108,31,8,0xE8EBCF);
    }
    for(int n=0;n<28;++n){
        const int x=(n*29+int(s.seed%17))%120,y=73+(n*7)%42;
        if(x>31 && x<90)continue;
        p.box(x,y,1,3,0x73926C);p.box(x-2,y+1,5,1,0x8CA575);
        if(n%3==0)p.star(x,y-1,n%2?0xF6DDA0:0xE8A6A0);
    }
    if(s.crop!=core::garden::None){
        p.box(7,96,21,12,0x9B775C);p.box(8,97,19,2,0xBC9571);
        for(int n=0;n<3;++n){p.box(11+n*6,94,1,9,0x527F61);p.box(9+n*6,96,5,2,0x86AD70);if(s.crop==4)p.oval(11+n*6,93,2,3,0xE49B7E);}
    }
    if(s.decor==1){p.oval(96,84,7,3,0xD2C78D);p.box(94,62,2,22,0x8A8064);p.box(90,58,10,9,0x847353);p.box(92,59,6,6,0xF8DB91);p.box(93,60,3,4,0xFFF1BE);}
    if(s.decor==2){p.box(86,70,20,17,0xBBA886);p.box(88,72,16,13,0xE6D9AF);p.box(83,67,25,4,0xAE7B70);p.box(86,64,19,3,0xCE9684);p.box(93,76,6,11,0x886F60);}
    if(s.decor==3){p.box(88,78,22,4,0xA08261);p.box(90,82,3,8,0x8C735A);p.box(105,82,3,8,0x8C735A);p.box(92,73,14,5,0xF3DCAB);p.box(98,74,1,4,0xA58F6C);}
    if(view.pet){
        const auto phase=core::garden::stage(s);
        unsigned sprite=view.preview>=0?unsigned(view.preview):phase==0?12:phase==1?13:s.form==core::garden::None?core::garden::route(s)*2:s.form;
        if(sprite>=garden_art::Count)sprite=12;
        constexpr int jumps[]={0,2,5,8,10,10,8,5,2,0,1,0};
        const unsigned reaction=std::min(view.reaction,12u),size=std::min(view.petSize,60u);
        const int jump=reaction?jumps[reaction-1]:0;
        const int x0=view.petX-int(size)/2,y0=view.ground-int(size)-jump-(frame%12<6?1:0);
        p.oval(view.petX,view.ground+1,17-jump/3,3,0x9CA984);p.oval(view.petX,view.ground,12-jump/3,2,0x899E7D);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            const char code=garden_art::Sprites[sprite][(y*32/size)*32+x*32/size];const unsigned index=code<='9'?code-'0':code-'a'+10;
            if(index)p.dot(x0+x,y0+y,garden_art::Palettes[sprite][index]);
        }
        if((reaction || frame%43>39 || s.energy<15) && view.preview<0){
            for(int ex:{10,20}){const int xx=x0+ex*int(size)/32,yy=y0+18*int(size)/32;
                p.box(xx,yy,4,6,garden_art::Palettes[sprite][2]);p.box(xx,yy+2,4,1,garden_art::Palettes[sprite][1]);
                if(reaction)p.dot(xx-1,yy+3,garden_art::Palettes[sprite][1]);
            }
        }
        if(reaction){
            const int lift=int(reaction)*2;
            for(int n=0;n<4;++n){
                const int x=view.petX+(n%2?1:-1)*(23+(n/2)*9),y=view.ground-22-(n/2)*17-lift;
                if((unsigned(n)+view.variant)%3)p.heart(x-3,y);else p.star(x,y,0xFFF2B6);
            }
            if(reaction<8){p.star(x0-4,y0+15,0xFFE7A1);p.star(x0+int(size)+3,y0+25,0xFFE7A1);}
        }else if((sprite&1) && sprite<12){
            for(int n=0;n<3;++n)p.star(x0-3+n*int(size)/2,y0+13+(n%2)*28,(frame+n)%3?0xEACC7C:0xFFFBE0);
        }
    }
    if(s.weather==1)for(int n=0;n<18;++n){const int x=(n*17+frame)%120,y=22+(n*11+frame*2)%69;p.box(x,y,1,3,0x9DBDB8);}
}
}
