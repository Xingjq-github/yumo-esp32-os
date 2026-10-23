#pragma once
#include "ui/app.hpp"
#include "ui/app_icons.hpp"
#include "core/mp3_player.hpp"
namespace apps {
class Mp3Music final:public ui::App {
public:
    const char *name()const override{return "MP3音乐";}
    const char *icon()const override{return ui::icons::Music;}
    void onCreate(lv_obj_t*)override;
    void onHide()override;
    void onDestroy()override;
    bool onBack()override;
private:
    void show();void poll();void action(int);void choose(int);void refresh();void focusText(const char*);
    lv_obj_t *iconButton(const char*,int,int,int,int,int,bool=false);
    lv_obj_t *button(const char*,int,int,int,int,int=36);
    lv_obj_t *label(const char*,int,int,int,bool=false);
    static void click(lv_event_t*);static void tick(lv_timer_t*);static void seek(lv_event_t*);
    lv_obj_t *host=nullptr,*pageRoot=nullptr,*track=nullptr,*status=nullptr,*position=nullptr,*duration=nullptr,*slider=nullptr,*playButton=nullptr;
    lv_obj_t *lyric[3]{},*repeatButton=nullptr,*lyricFocus=nullptr;
    lv_timer_t *timer=nullptr;
    std::shared_ptr<core::mp3::Listing> listing;
    std::shared_ptr<core::mp3::Player> player;
    std::vector<core::mp3::Entry> entries;
    std::string notice;
    int page=0,selected=-1,pending=-1,lyricIndex=-2;
    uint32_t deadline=0;
    bool playback=true,repeat=false,truncated=false;
};
}
