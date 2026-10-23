#include "mp3_music.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include <algorithm>
#include <cstdio>
LV_FONT_DECLARE(lv_font_music_22);
namespace apps {
namespace c=ui::compact;
using core::mp3::State;
namespace {
void clockText(lv_obj_t *obj,uint32_t ms) {
    if(!obj)return;
    const auto s=ms/1000;char text[24];std::snprintf(text,sizeof(text),"%02u:%02u",unsigned(s/60),unsigned(s%60));c::setText(obj,text);
}

}
lv_obj_t *Mp3Music::iconButton(const char *text,int id,int x,int y,int w,int h,bool primary) {
    auto b=button(text,id,x,y,w,h);
    lv_obj_set_style_border_width(b,0,LV_PART_MAIN);
    lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_opa(b,primary?LV_OPA_COVER:LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(b,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b,LV_OPA_COVER,LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b,primary?LV_OPA_COVER:LV_OPA_TRANSP,LV_STATE_DISABLED);
    if(primary)c::buttonBackground(b,ui::theme::accent());
    if(auto l=lv_obj_get_child(b,0)){
        lv_obj_set_style_text_font(l,&lv_font_montserrat_24,0);
        lv_obj_center(l);
    }
    return b;
}
void Mp3Music::focusText(const char *text) {
    // Two readable lines when they fit; long titles/lyrics remain accessible
    // through a single-line marquee instead of disappearing below the box.
    lv_point_t size{};lv_text_get_size(&size,text,&lv_font_music_22,0,2,208,LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(track,size.y>60?LV_LABEL_LONG_MODE_SCROLL:LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(track,LV_SIZE_CONTENT);c::setText(track,text);lv_obj_center(track);
}
lv_obj_t *Mp3Music::button(const char *text,int id,int x,int y,int w,int h) {
    auto b=c::button(pageRoot,text,click,this,w,h);lv_obj_set_pos(b,x,y);
    lv_obj_set_user_data(b,reinterpret_cast<void*>(intptr_t(id)));
    if(auto l=lv_obj_get_child(b,0)){lv_obj_set_width(l,w-8);lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);}
    return b;
}
lv_obj_t *Mp3Music::label(const char *text,int x,int y,int w,bool muted) {
    auto l=c::label(pageRoot,text,w);lv_obj_set_pos(l,x,y);
    if(muted)lv_obj_set_style_text_color(l,ui::theme::muted(),0);
    return l;
}
void Mp3Music::onCreate(lv_obj_t *p) {
    host=p;lv_obj_set_scrollable(p,false);lv_obj_set_style_pad_all(p,0,0);
    refresh();timer=lv_timer_create(tick,100,this);
}
void Mp3Music::onHide() {
    pending=-1;if(listing)listing->cancel=true;if(player)player->cancel=true;
    if(timer){lv_timer_delete(timer);timer=nullptr;}
    listing.reset();player.reset();std::vector<core::mp3::Entry>().swap(entries);
}
void Mp3Music::onDestroy(){onHide();}
bool Mp3Music::onBack(){if(playback)return false;playback=true;show();poll();return true;}
void Mp3Music::refresh() {
    if(listing)return;
    if(player)player->cancel=true;
    player.reset();selected=pending=-1;entries.clear();page=0;
    listing=core::mp3::scan();notice=listing?"读取 SD /MP3…":"SD卡忙，请稍后刷新";show();
}
void Mp3Music::choose(int index) {
    if(index<0||size_t(index)>=entries.size())return;
    playback=true;
    if(selected==index&&player&&!player->done.load()){show();return;}
    if(player)player->cancel=true;
    player.reset();pending=selected=index;deadline=lv_tick_get()+6000;
    notice="加载音乐…";lyricIndex=-2;show();
}
void Mp3Music::show() {
    if(pageRoot)lv_obj_delete(pageRoot);
    pageRoot=c::panel(host,240,240);lv_obj_set_pos(pageRoot,0,0);
    track=status=position=duration=slider=playButton=repeatButton=lyricFocus=nullptr;
    for(auto &p:lyric)p=nullptr;
    lyricIndex=-2;
    if(!playback) {
        // List is an in-app sheet: close returns to music, never exits the app.
        iconButton(LV_SYMBOL_DOWN,14,0,0,40,36);
        char count[32];std::snprintf(count,sizeof(count),"%u 首",unsigned(entries.size()));
        auto title=label(count,45,8,146,true);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        c::disabled(iconButton(LV_SYMBOL_REFRESH,2,200,0,40,36),bool(listing));
        if(entries.empty()) {
            auto n=label(notice.c_str(),16,85,208,true);lv_label_set_long_mode(n,LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_style_text_align(n,LV_TEXT_ALIGN_CENTER,0);
        }else {
            for(int row=0;row<4;++row) {
                const int index=page*4+row;if(size_t(index)>=entries.size())break;
                auto b=button("",100+row,8,38+row*40,224,39);
                lv_obj_set_style_border_width(b,0,0);lv_obj_set_style_radius(b,8,0);
                lv_obj_set_style_bg_opa(b,index==selected?LV_OPA_COVER:LV_OPA_TRANSP,0);
                auto number=c::label(b,index==selected?LV_SYMBOL_AUDIO:LV_SYMBOL_PLAY,24);lv_obj_set_pos(number,8,8);
                lv_obj_set_style_text_color(number,ui::theme::accent(),0);
                auto title=c::label(b,entries[index].title.c_str(),174);lv_obj_set_pos(title,38,8);
            }
            c::disabled(iconButton(LV_SYMBOL_LEFT,3,0,201,52,39),page==0);
            c::disabled(iconButton(LV_SYMBOL_RIGHT,4,188,201,52,39),size_t((page+1)*4)>=entries.size());
            char text[40];std::snprintf(text,sizeof(text),"%d / %u%s",page+1,unsigned((entries.size()+3)/4),truncated?" +":"");
            auto n=label(text,54,211,132,true);lv_obj_set_style_text_align(n,LV_TEXT_ALIGN_CENTER,0);
        }
        return;
    }
    // A continuous lyric stage, a slim timeline and three large transport
    // controls. Secondary actions have their own row and no card backgrounds.
    lv_obj_set_style_bg_color(pageRoot,lv_color_mix(ui::theme::accent(),ui::theme::bg(),22),0);
    lv_obj_set_style_bg_grad_color(pageRoot,ui::theme::bg(),0);
    lv_obj_set_style_bg_grad_dir(pageRoot,LV_GRAD_DIR_VER,0);
    lyric[0]=label(LV_SYMBOL_AUDIO,16,8,208,true);
    lyric[2]=label("",16,104,208,true);
    for(int i:{0,2}) {lv_obj_set_height(lyric[i],24);lv_obj_set_style_text_align(lyric[i],LV_TEXT_ALIGN_CENTER,0);}
    lyricFocus=c::panel(pageRoot,208,60);lv_obj_set_pos(lyricFocus,16,36);
    lv_obj_set_style_bg_opa(lyricFocus,LV_OPA_TRANSP,0);lv_obj_set_clickable(lyricFocus,false);
    track=lyric[1]=c::label(lyricFocus,"",208);
    lv_obj_set_style_text_font(track,&lv_font_music_22,0);
    lv_obj_set_style_text_line_space(track,2,0);
    lv_obj_set_style_text_align(track,LV_TEXT_ALIGN_CENTER,0);
    focusText(selected>=0?entries[selected].title.c_str():notice.c_str());
    status=label("",16,104,208,true);lv_obj_set_height(status,24);
    lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
    slider=lv_slider_create(pageRoot);lv_obj_set_pos(slider,20,132);lv_obj_set_size(slider,200,4);
    c::disabled(slider,true);lv_slider_set_range(slider,0,1000);lv_obj_set_ext_click_area(slider,8);
    lv_obj_set_style_bg_color(slider,ui::theme::edge(),LV_PART_MAIN);lv_obj_set_style_radius(slider,LV_RADIUS_CIRCLE,LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,ui::theme::accent(),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,ui::theme::fg(),LV_PART_KNOB);lv_obj_set_style_pad_all(slider,3,LV_PART_KNOB);
    lv_obj_add_event_cb(slider,seek,LV_EVENT_RELEASED,this);
    position=label("00:00",46,153,66,true);duration=label("--:--",128,153,66,true);
    lv_obj_set_style_text_font(position,&lv_font_montserrat_14,0);lv_obj_set_style_text_font(duration,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_align(duration,LV_TEXT_ALIGN_RIGHT,0);
    repeatButton=iconButton(LV_SYMBOL_LOOP,12,0,144,40,36);
    lv_obj_set_style_text_font(lv_obj_get_child(repeatButton,0),&lv_font_montserrat_18,0);
    c::checked(repeatButton,repeat);
    c::disabled(iconButton(LV_SYMBOL_PREV,5,28,184,48,48),entries.empty());
    playButton=iconButton(LV_SYMBOL_PLAY,6,88,174,64,64,true);
    c::disabled(playButton,selected<0||pending>=0||bool(listing));
    c::disabled(iconButton(LV_SYMBOL_NEXT,7,164,184,48,48),entries.empty());
    auto listButton=iconButton(LV_SYMBOL_LIST,13,200,144,40,36);
    lv_obj_set_style_text_font(lv_obj_get_child(listButton,0),&lv_font_montserrat_18,0);
}
void Mp3Music::poll() {
    if(listing&&listing->done.load(std::memory_order_acquire)) {
        entries=std::move(listing->entries);notice=listing->error;truncated=listing->truncated;listing.reset();
        const auto last=sys::Settings::mp3Track();
        const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.path==last;});
        selected=entries.empty()?-1:it==entries.end()?0:int(it-entries.begin());
        page=selected<0?0:selected/4;
        if(entries.empty()&&notice.empty())notice="SD /MP3 中没有音乐";
        show();
    }
    if(pending>=0) {
        if(core::mp3::idle())player=core::mp3::start(entries[pending].path);
        if(player)pending=-1;
        else if(int32_t(lv_tick_get()-deadline)>=0){pending=-1;notice="音频忙，请重试";}
    }
    if(player&&player->done.load()&&player->state.load()==State::Ended) {
        const auto next=repeat?selected:(selected+1)%int(entries.size());
        player.reset();choose(next);return;
    }
    if(!playback)return;
    const auto s=player?player->state.load(std::memory_order_acquire):State::Loading;
    const bool ready=player&&(s==State::Playing||s==State::Paused);
    c::disabled(slider,!ready);c::disabled(playButton,selected<0||bool(listing)||pending>=0||(player&&(s==State::Loading||s==State::Seeking)));
    c::setText(lv_obj_get_child(playButton,0),player&&player->play.load()&&s==State::Playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    if(player) {
        clockText(position,player->position.load());clockText(duration,player->duration.load());
        if(!lv_slider_is_dragged(slider)&&player->duration.load())lv_slider_set_value(slider,uint64_t(player->position.load())*1000/player->duration.load(),LV_ANIM_OFF);
    }
    if(player&&s==State::Failed)c::setText(status,player->error.c_str());
    else if(s==State::Seeking)c::setText(status,"定位中…");
    else if(player&&s==State::Loading)c::setText(status,"加载音乐…");
    else c::setText(status,!player&&selected>=0?notice.c_str():"");
    lv_obj_set_hidden(lyric[2],lv_label_get_text(status)[0]!=0);
    const bool metadata=player&&player->metadataReady.load(std::memory_order_acquire);
    const bool hasLyrics=metadata&&!player->lyrics.empty();
    const int at=hasLyrics?core::mp3::lyricAt(player->lyrics,player->position.load()):-3;
    if(at!=lyricIndex) {
        lyricIndex=at;
        for(auto l:lyric)c::setText(l,"");
        if(hasLyrics&&at>=0) {
            for(int i:{0,2}){int index=at+i-1;c::setText(lyric[i],index>=0&&size_t(index)<player->lyrics.size()?player->lyrics[index].text.c_str():"");}
            focusText(player->lyrics[at].text.c_str());
        }else {
            c::setText(lyric[0],LV_SYMBOL_AUDIO);
            focusText(selected>=0?entries[selected].title.c_str():notice.c_str());
            if(hasLyrics)c::setText(lyric[2],player->lyrics.front().text.c_str());
        }
    }

}
void Mp3Music::action(int id) {
    if(id==14){playback=true;show();poll();return;}
    if(id==2){refresh();return;}
    if(id==13){playback=false;page=selected<0?0:selected/4;show();return;}
    if(id==3||id==4){page+=id==3?-1:1;show();return;}
    if(id>=100){choose(page*4+id-100);return;}
    if(entries.empty()){poll();return;}
    if(id==5||id==7){choose((selected+int(entries.size())+(id==5?-1:1))%int(entries.size()));return;}
    if(id==12){repeat=!repeat;c::checked(repeatButton,repeat);}
    if(id==6) {
        if(!player||player->done.load()) {player.reset();choose(selected);return;}
        player->play=!player->play.load();
    }
    poll();
}
void Mp3Music::click(lv_event_t *e){static_cast<Mp3Music*>(lv_event_get_user_data(e))->action(int(intptr_t(lv_obj_get_user_data(lv_event_get_target_obj(e)))));}
void Mp3Music::tick(lv_timer_t *t){static_cast<Mp3Music*>(lv_timer_get_user_data(t))->poll();}
void Mp3Music::seek(lv_event_t *e) {
    auto self=static_cast<Mp3Music*>(lv_event_get_user_data(e));
    if(self->player)self->player->seekMs=uint64_t(lv_slider_get_value(self->slider))*self->player->duration.load()/1000;
}
}
