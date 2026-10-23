#pragma once
#include "random_model.hpp"
#include "core/theme_model.hpp"

// Display-only geometry, shared by all modes; never owns RNG or input state.
namespace apps::random_layout {
using Id=sys::themes::Id;
using Rect=random_model::Cell;
struct Number { Rect result,hint,lowTitle,highTitle,low,high; };
constexpr Number number(Id id) {
    if(id==Id::Tiles) return {{0,0,145,136},{0,139,240,22},{149,0,91,22},
        {149,77,91,22},{149,23,91,36},{149,100,91,36}};
    if(id==Id::Warm || id==Id::Focus) return {{4,0,232,80},{0,81,240,22},
        {2,104,117,22},{121,104,117,22},{2,126,117,36},{121,126,117,36}};
    return {{0,60,240,77},{0,138,240,22},{0,0,119,22},
        {120,0,120,22},{0,23,119,36},{120,23,120,36}};
}
constexpr int radius(Id id) { return id==Id::Warm?12:id==Id::Tiles?9:id==Id::Focus?2:0; }
constexpr Rect cell(Id id,unsigned count,unsigned index,int height) {
    if(!count || count>random_model::MaxChoices || index>=count) return {0,0,0,0};
    const int inset=id==Id::Warm?3:0;
    const int gap=id==Id::Warm?4:id==Id::Focus?3:id==Id::Tiles?4:1;
    const unsigned cols=count<=3?count:count==4?2:count<=9?(id==Id::Focus && count<=8 && count!=5?2:3):4;
    const unsigned rows=(count+cols-1)/cols,row=index/cols,col=index%cols;
    const unsigned left=count-row*cols,n=left<cols?left:cols;
    const int w=240-2*inset-int(n-1)*gap,h=height-2*inset-int(rows-1)*gap;
    const int x=inset+w*int(col)/int(n)+int(col)*gap,y=inset+h*int(row)/int(rows)+int(row)*gap;
    return {x,y,w*int(col+1)/int(n)-w*int(col)/int(n),h*int(row+1)/int(rows)-h*int(row)/int(rows)};
}
constexpr Rect ball(Id id,unsigned index) {
    if(id==Id::Focus) return index==6?Rect{90,119,60,40}:Rect{int(index%3)*81,25+int(index/3)*46,78,42};
    if(id==Id::Tiles) return index==6?Rect{176,25,64,133}:Rect{int(index%3)*58,25+int(index/3)*68,54,64};
    return index==6?Rect{178,62,60,60}:Rect{int(index%3)*55,29+int(index/3)*60,52,52};
}
} // namespace apps::random_layout
