#include "ui/quick_panel_model.hpp"
using ui::quick::Swipe;
using ui::quick::Route;
constexpr bool edgeSwipe() {
    Swipe s;
    if(s.update(true,100,5,0,true,false)!=Route::Hold)return false;
    if(s.update(true,100,20,20,true,false)!=Route::Hold)return false;
    if(s.update(true,102,40,40,true,false)!=Route::Open)return false;
    if(s.update(true,130,180,60,true,true)!=Route::Consume)return false;
    if(s.update(false,130,180,80,true,true)!=Route::Consume)return false;
    return s.update(true,100,231,100,true,true)==Route::Hold &&
        s.update(true,103,191,120,true,true)==Route::Close;
}
constexpr bool tapsAndOtherGestures() {
    Swipe s;
    if(s.update(true,10,4,0,true,false)!=Route::Hold)return false;
    if(s.update(false,10,4,20,true,false)!=Route::Pass)return false;
    if(s.update(true,10,4,40,true,false)!=Route::Hold)return false;
    if(s.update(true,50,5,60,true,false)!=Route::Pass)return false;
    if(s.update(true,50,60,80,true,false)!=Route::Pass)return false;
    s.update(false,50,60,100,true,false);
    if(s.update(true,50,30,120,true,false)!=Route::Pass)return false;
    return s.update(true,50,100,140,true,false)==Route::Pass;
}
constexpr bool excludedAndSlider() {
    Swipe s;
    for(unsigned t=0;t<5;++t)if(s.update(true,10,10+t*10,t*20,false,false)!=Route::Pass)return false;
    s.update(false,10,60,100,false,false);
    if(s.update(true,90,52,120,true,true)!=Route::Pass)return false;
    return s.update(true,190,10,140,true,true)==Route::Pass;
}
constexpr bool cancelAndWrap() {
    Swipe s;
    if(s.update(true,20,5,0xfffffff0,true,false)!=Route::Hold)return false;
    if(s.update(true,20,35,20,true,false)!=Route::Open)return false;
    s.cancel();
    if(s.update(false,20,35,40,true,false)!=Route::Consume)return false;
    if(s.update(true,20,5,60,true,false)!=Route::Hold)return false;
    if(s.update(true,20,10,561,true,false)!=Route::Pass)return false;
    return s.update(true,20,50,580,true,false)==Route::Pass;
}
static_assert(edgeSwipe());
static_assert(tapsAndOtherGestures());
static_assert(excludedAndSlider());
static_assert(cancelAndWrap());
static_assert(ui::quick::Layout::Margin*2+ui::quick::Layout::TileW*3+ui::quick::Layout::Gap*2==240);
