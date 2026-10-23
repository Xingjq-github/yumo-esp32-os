#pragma once
#include "compact_ui.hpp"
#include "theme.hpp"
#include "app_icons.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ui {
// One reusable card, not one preloaded App/page per item. No timer or animation
// buffer. The owner supplies display data and queues its existing navigation.
class FocusPager {
public:
    using Callback=void (*)(void *,size_t);
    void reset() { root_=card_=number_=icon_=title_=detail_=address_=progress_=nullptr; }
    void create(lv_obj_t *parent,int height,size_t count,size_t selected,
                void *owner,Callback render,Callback open) {
        reset();count_=count;index_=count?selected%count:0;owner_=owner;render_=render;open_=open;hold_=nullptr;
        root_=compact::panel(parent,240,height);
        auto heading=compact::label(root_,"YUMO / CARDS",160);
        lv_obj_set_style_text_font(heading,&lv_font_montserrat_12,0);lv_obj_set_pos(heading,8,4);
        number_=compact::label(root_,"",66);lv_obj_set_pos(number_,166,2);
        lv_obj_set_style_text_font(number_,&lv_font_montserrat_14,0);
        lv_obj_set_style_text_align(number_,LV_TEXT_ALIGN_RIGHT,0);
        card_=compact::button(root_,"",nullptr,nullptr,240,height-69);lv_obj_set_pos(card_,0,24);
        lv_obj_set_gesture_bubble(card_,false);
        lv_obj_add_event_cb(card_,event,LV_EVENT_ALL,this);
        auto badge=compact::panel(card_,70,70);lv_obj_set_pos(badge,8,5);
        lv_obj_set_style_bg_color(badge,theme::accent(),0);lv_obj_set_style_radius(badge,18,0);
        lv_obj_set_clickable(badge,false);
        icon_=compact::label(badge,"");lv_obj_set_style_text_font(icon_,icons::font48(),0);
        lv_obj_set_style_text_color(icon_,theme::onAccent(),0);lv_obj_center(icon_);
        title_=compact::label(card_,"",218);lv_obj_set_pos(title_,8,80);
        detail_=compact::label(card_,"",218);lv_obj_set_pos(detail_,8,105);
        address_=compact::label(card_,"",218);lv_obj_set_pos(address_,8,128);
        lv_obj_set_style_text_font(address_,&lv_font_montserrat_10,0);
        auto rail=compact::panel(root_,224,3);lv_obj_set_pos(rail,8,height-42);
        lv_obj_set_style_bg_color(rail,theme::edge(),0);
        progress_=compact::panel(rail,1,3);lv_obj_set_style_bg_color(progress_,theme::accent(),0);
        auto prev=compact::button(root_,LV_SYMBOL_LEFT,[](lv_event_t *e){self(e)->move(-1);},this,47,36);
        auto enter=compact::button(root_,"打开",[](lv_event_t *e){self(e)->activate();},this,144,36);
        auto next=compact::button(root_,LV_SYMBOL_RIGHT,[](lv_event_t *e){self(e)->move(1);},this,47,36);
        lv_obj_set_pos(prev,0,height-36);lv_obj_set_pos(enter,48,height-36);lv_obj_set_pos(next,193,height-36);
        compact::buttonBackground(enter,theme::accent());
        update();
    }
    void content(const char *title,const char *icon,const char *detail,const char *address="") {
        if(!root_)return;
        compact::setText(title_,title);compact::setText(icon_,icon);compact::setText(detail_,detail);
        compact::setText(address_,address);lv_obj_center(icon_);
    }
    size_t selected() const {return index_;}
    void onLongPress(Callback callback){hold_=callback;}
    void select(size_t index){if(count_){index_=index%count_;update();}}
private:
    static FocusPager *self(lv_event_t *e) {return static_cast<FocusPager *>(lv_event_get_user_data(e));}
    static void event(lv_event_t *e) {
        auto p=self(e);const auto code=lv_event_get_code(e);auto input=lv_indev_active();
        if(code==LV_EVENT_PRESSED && input) {lv_indev_get_point(input,&p->press_);p->dragged_=false;p->held_=false;}
        if(code==LV_EVENT_PRESSING && input) {
            lv_point_t point{};lv_indev_get_point(input,&point);
            if(std::abs(point.x-p->press_.x)>18 || std::abs(point.y-p->press_.y)>18)p->dragged_=true;
        }
        if(code==LV_EVENT_PRESS_LOST)p->dragged_=true;
        if(code==LV_EVENT_LONG_PRESSED && input && p->hold_ && !p->dragged_){
            p->held_=p->dragged_=true;p->hold_(p->owner_,p->index_);
        }
        if(code==LV_EVENT_RELEASED && input && p->dragged_ && !p->held_) {
            lv_point_t point{};lv_indev_get_point(input,&point);
            const int dx=point.x-p->press_.x,dy=point.y-p->press_.y;
            if(std::abs(dx)>28 && std::abs(dx)>std::abs(dy))p->move(dx<0?1:-1);
        }
        if(code==LV_EVENT_CLICKED && !p->dragged_)p->activate();
    }
    void move(int delta) {if(!count_)return;index_=delta<0?(index_?index_-1:count_-1):(index_+1)%count_;update();}
    void activate() {if(count_ && open_)open_(owner_,index_);}
    void update() {
        char text[24];std::snprintf(text,sizeof(text),"%02u / %02u",unsigned(index_+1),unsigned(count_));
        compact::setText(number_,text);
        const int width=count_?224/int(count_):224;
        lv_obj_set_width(progress_,width);lv_obj_set_x(progress_,int(index_)*width);
        if(render_)render_(owner_,index_);
    }
    lv_obj_t *root_=nullptr,*card_=nullptr,*number_=nullptr,*icon_=nullptr,*title_=nullptr;
    lv_obj_t *detail_=nullptr,*address_=nullptr,*progress_=nullptr;
    size_t count_=0,index_=0;void *owner_=nullptr;Callback render_=nullptr,open_=nullptr,hold_=nullptr;
    lv_point_t press_{};bool dragged_=false,held_=false;
};
} // namespace ui
