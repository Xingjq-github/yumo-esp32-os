#pragma once
#include "maze_model.hpp"
#include <cstdint>
namespace maze::visual {
constexpr uint32_t floorColors[5]={0x5F554A,0x6D7272,0x78634B,0x575C65,0x637172};
constexpr uint32_t wallColors[5]={0xB09A79,0xBEC3C2,0xAA8058,0x9BA1AA,0xADC2C1};
constexpr uint32_t ballColors[5]={0xAABBC8,0xD6CAB5,0xB77C3E,0xAB4840,0x79B9BE};
constexpr int channel(int value){return value<0?0:value>255?255:value;}
constexpr uint32_t shade(uint32_t c,int d){return (uint32_t(channel(int(c>>16&255)+d))<<16)|(uint32_t(channel(int(c>>8&255)+d))<<8)|channel(int(c&255)+d);}
constexpr uint16_t rgb565(uint32_t c){return uint16_t((c>>8&0xf800)|(c>>5&0x7e0)|(c>>3&31));}
constexpr uint32_t noise(unsigned x,unsigned y){unsigned v=x*374761393u+y*668265263u;v=(v^(v>>13))*1274126177u;return v^(v>>16);}
constexpr uint32_t trapColor(Hazard kind){return kind==Hazard::Sand?0xAC8D59:kind==Hazard::Ice?0x547E8B:kind==Hazard::Bumper?0x7D7269:0x171B21;}
constexpr uint32_t rimColor(uint32_t floor){return shade(floor,18);}
inline void background(uint16_t *pixels,const Board &b,unsigned level) {
    const unsigned s=stage(level);const uint32_t color=floorColors[s];
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x) {
        int grain=int(noise(x,y)%13)-6;
        if(s==0||s==2)grain+=int((y*3+x/23)%17)-8; // long wood grain
        else if(s==1)grain+=y%3==0?4:-2; // brushed metal
        else if(noise(x/3,y/3)%9==0)grain-=9; // stone flecks
        pixels[y*240+x]=rgb565(shade(color,grain));
    }
    auto rect=[&](int x,int y,int w,int h,uint32_t c) {
        for(int yy=y;yy<y+h&&yy<240;++yy)for(int xx=x;xx<x+w&&xx<240;++xx)
            if(xx>=0&&yy>=0)pixels[yy*240+xx]=rgb565(c);
    };
    eachWall(b,[&](Rect r){rect(int(r.x)+2,int(r.y)+2,int(r.w)+1,int(r.h)+1,shade(color,-22));});
    eachWall(b,[&](Rect r){
        const auto c=wallColors[s];rect(int(r.x),int(r.y),int(r.w),int(r.h),c);
        const bool horizontal=r.w>r.h;
        rect(int(r.x),int(r.y),horizontal?int(r.w):1,horizontal?1:int(r.h),shade(c,28));
        rect(int(r.x)+(horizontal?0:2),int(r.y)+(horizontal?2:0),horizontal?int(r.w):1,horizontal?1:int(r.h),shade(c,-30));
    });
    for(unsigned i=0;i<b.trapCount;++i){
        const auto t=b.traps[i];const auto r=t.area;
        for(int y=std::max(0,int(r.y));y<std::min(240,int(r.y+r.h));++y)
          for(int x=std::max(0,int(r.x));x<std::min(240,int(r.x+r.w));++x)if(touchesTrap(t,x+.5f,y+.5f)) {
            const bool rim=hardTrap(t.kind)&&!touchesHazard(t,x+.5f,y+.5f);
            uint32_t c=rim?rimColor(color):trapColor(t.kind);
            int grain=int(noise(x,y)%13)-6;
            const bool top=!touchesHazard(t,x+.5f,y-1.5f),bottom=!touchesHazard(t,x+.5f,y+2.5f);
            if(rim){grain=int(noise(x,y)%25)-12;}
            else if(t.kind==Hazard::Pit){grain=top?-20:bottom?36:-6;if(top)c=shade(color,-32);}
            else if(t.kind==Hazard::Ice){grain=top?36:bottom?-22:((y+int(i)*3)%7==0?14:0);}
            else {if(top)grain+=26;if(bottom)grain-=26;}
            pixels[y*240+x]=rgb565(shade(c,grain));
        }
    }
}
inline void sphere(uint32_t *pixels,const Board &b,const Ball &ball) {
    const unsigned m=unsigned(b.material);const float r=ball.falling?6.f*ball.falling/24.f:6.f;
    for(int y=0;y<12;++y)for(int x=0;x<12;++x) {
        const float dx=x-5.5f,dy=y-5.5f;
        if(dx*dx+dy*dy>r*r){pixels[y*12+x]=0;continue;}
        int light=23-int(dx*5+dy*5)-int((dx*dx+dy*dy)*1.5f);
        const int roll=int(ball.travel/2);
        if(m==2)light+=(x+y+roll+10000)%5<2?-23:4;
        else if(m==1)light+=((x*2+y+roll+10000)%9==0)?-29:0;
        else if(m==3)light+=int(noise(x+roll,y)%9)-4;
        else if((x-3)*(x-3)+(y-2)*(y-2)<5)light+=m==4?70:85;
        pixels[y*12+x]=0xff000000|shade(ballColors[m],light);
    }
}
}
