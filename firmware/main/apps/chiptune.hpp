#pragma once
#include "ui/app.hpp"
#include "core/chiptune_player.hpp"
namespace apps {
class Chiptune final:public ui::App {
public:
    const char *name()const override{return "8Bit音乐";}
    const char *icon()const override{return LV_SYMBOL_PLAY;}
    void onCreate(lv_obj_t *)override;
    void onHide()override;
    void onDestroy()override;
    bool onBack()override;
private:
    void refresh();void choose(int);void show();void poll();void action(int);
    lv_obj_t *button(const char *,int,int,int,int);
    static void click(lv_event_t *);static void tick(lv_timer_t *);
    lv_obj_t *host=nullptr,*pageRoot=nullptr,*status=nullptr,*title=nullptr,*position=nullptr,
        *playButton=nullptr,*loopButton=nullptr,*previous=nullptr,*next=nullptr,*stopButton=nullptr,*volume=nullptr;
    lv_timer_t *timer=nullptr;
    lv_obj_t *rows[4]{};
    std::shared_ptr<core::chip::Listing> listing;
    std::shared_ptr<core::chip::Player> player;
    std::vector<core::chip::Entry> entries;
    std::string notice;
    int page=0,selected=-1,pending=-1;
    uint32_t waitUntil=0;
    bool playback=false,truncated=false;
};
}
