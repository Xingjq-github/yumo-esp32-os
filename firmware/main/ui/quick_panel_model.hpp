#pragma once
#include <cstdint>
namespace ui::quick {
struct Layout {
    static constexpr int Width=240,Height=240,Edge=18,Threshold=28;
    static constexpr int Margin=6,Gap=6,SliderY=6,SliderH=53,SliderGap=4;
    static constexpr int TileY=122,TileW=72,TileH=50,ActionY=178,ActionW=111,ActionH=40;
    static constexpr int HandleY=ActionY+ActionH;
    static constexpr bool slider(int x,int y) {
        return x>=Margin&&x<Width-Margin&&y>=SliderY&&y<SliderY+2*SliderH+SliderGap;
    }
};
enum class Route {Pass,Hold,Open,Close,Consume};
// During an edge candidate keep the original press coordinate. A tap still
// reaches its real widget; a vertical swipe is claimed before a list scrolls.
struct Swipe {
    bool down=false,candidate=false,closing=false,captured=false;
    int x=0,y=0;
    uint32_t began=0;
    constexpr void cancel(){candidate=false;captured=down;}
    constexpr Route update(bool pressed,int px,int py,uint32_t now,bool eligible,bool panel) {
        if(captured) {if(!pressed){captured=down=false;}return Route::Consume;}
        if(!pressed){down=candidate=false;return Route::Pass;}
        if(!down) {
            down=true;x=px;y=py;began=now;closing=panel;
            candidate=eligible&&(panel?py>=Layout::HandleY&&py<Layout::Height:py>=0&&py<Layout::Edge);
        }
        if(!candidate)return Route::Pass;
        const int dx=px-x,dy=py-y,side=dx<0?-dx:dx,distance=closing?-dy:dy;
        if(distance>=Layout::Threshold&&distance>side*2) {
            candidate=false;captured=true;return closing?Route::Close:Route::Open;
        }
        if(side>=14||distance<-8||uint32_t(now-began)>500) {candidate=false;return Route::Pass;}
        return Route::Hold;
    }
};
}
