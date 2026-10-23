#include "apps/random_layout.hpp"
namespace l=apps::random_layout;
constexpr bool inside(l::Rect r,int height=162) {
    return r.x>=0 && r.y>=0 && r.width>0 && r.height>0 && r.x+r.width<=240 && r.y+r.height<=height;
}
constexpr bool separate(l::Rect a,l::Rect b) {
    return a.x+a.width<=b.x || b.x+b.width<=a.x || a.y+a.height<=b.y || b.y+b.height<=a.y;
}
constexpr bool layouts() {
    for(auto id:{l::Id::Warm,l::Id::Mono,l::Id::Tiles,l::Id::Focus,l::Id::Terminal}) {
        const auto n=l::number(id);
        const l::Rect parts[]={n.result,n.hint,n.lowTitle,n.highTitle,n.low,n.high};
        for(unsigned i=0;i<6;++i) {
            if(!inside(parts[i]))return false;
            for(unsigned j=0;j<i;++j)if(!separate(parts[i],parts[j]))return false;
        }
        if(n.low.height!=36 || n.high.height!=36 || n.low.width<90 || n.high.width<90)return false;
        for(unsigned count=2;count<=16;++count)for(int height:{125,162}) {
            for(unsigned i=0;i<count;++i) {
                const auto r=l::cell(id,count,i,height);
                if(!inside(r,height)||r.width<50||r.height<26)return false;
                for(unsigned j=0;j<i;++j)if(!separate(r,l::cell(id,count,j,height)))return false;
            }
        }
        for(unsigned i=0;i<7;++i) {
            if(!inside(l::ball(id,i)))return false;
            for(unsigned j=0;j<i;++j)if(!separate(l::ball(id,i),l::ball(id,j)))return false;
        }
    }
    return true;
}
static_assert(layouts(),"Random themes: content in 240x162, no overlap, 36px input targets");
