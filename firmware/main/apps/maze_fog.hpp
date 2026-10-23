#pragma once
#include "maze_model.hpp"
#include <cstring>
namespace maze {
// Mode 1 retains the swept visible trail; mode 2 erases the previous view.
// Full-resolution alpha avoids cell-shaped reveals. Work is bounded to the
// moving view's bounding box; no per-frame allocations or floating sqrt.
class Fog {
public:
    static constexpr unsigned Size=240*240;
    static constexpr unsigned mode(unsigned level){return level%3;}
    static constexpr unsigned opacity(bool developer){return developer?64:255;}
    void reset(uint8_t *bytes,const Board &board,unsigned level){
        data_=bytes;mode_=mode(level);radius_=board.roadWidth()*3/4;
        inner_=std::max(1,radius_*2/3);lastX_=lastY_=-1000;
        if(data_)std::memset(data_,mode_?255:0,Size);
    }
    bool update(int x,int y){
        if(!data_ || !mode_ || (x==lastX_ && y==lastY_))return false;
        if(mode_==2 && lastX_!=-1000) {
            for(int yy=std::max(0,lastY_-radius_);yy<=std::min(239,lastY_+radius_);++yy)
                std::memset(data_+yy*240+std::max(0,lastX_-radius_),255,
                    std::min(239,lastX_+radius_)-std::max(0,lastX_-radius_)+1);
        }
        const int r2=radius_*radius_,i2=inner_*inner_;
        for(int yy=std::max(0,y-radius_);yy<=std::min(239,y+radius_);++yy)
            for(int xx=std::max(0,x-radius_);xx<=std::min(239,x+radius_);++xx) {
                const int dx=xx-x,dy=yy-y,d2=dx*dx+dy*dy;
                const unsigned a=d2<=i2?0:d2>=r2?255:unsigned((d2-i2)*255/(r2-i2));
                auto &out=data_[yy*240+xx];out=uint8_t(std::min(unsigned(out),a));
            }
        lastX_=x;lastY_=y;return true;
    }
    int radius() const{return radius_;}
private:
    uint8_t *data_=nullptr;
    unsigned mode_=0;
    int radius_=1,inner_=1,lastX_=-1000,lastY_=-1000;
};
}
