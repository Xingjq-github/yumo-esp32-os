#include "timer.hpp"
#include "core/button_mgr.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace apps {
namespace {
namespace theme=ui::theme;
namespace compact=ui::compact;
constexpr uint32_t ClockBg=0x11171D,LapsBg=0x151D24,LiveBg=0x202C37;
constexpr uint32_t Normal=0xAAB5C0,Fast=0x56D89B,Slow=0xFF6B78;
lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int width,const lv_font_t *font=nullptr) {
    auto obj=compact::label(parent,value,width);lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_font(obj,font?font:theme::fontMenu(),0);
    return obj;
}
int numericWidth(const char *value,const lv_font_t *font) {
    int width=0;
    // Montserrat digits have different advances; "8" is not always widest.
    for(char digit='0';digit<='9';++digit) {
        char sample[48];std::snprintf(sample,sizeof(sample),"%s",value);
        for(char *p=sample;*p;++p)if(*p>='0' && *p<='9')*p=digit;
        lv_point_t size;lv_text_get_size(&size,sample,font,0,0,1000,LV_TEXT_FLAG_NONE);
        if(size.x>width)width=size.x;
    }
    return width+1;
}
void numericText(lv_obj_t *obj,const char *value,int height) {
    const auto previous=lv_label_get_text(obj);
    if(!std::strcmp(previous,value))return;
    // These fixed numeric formats only change their shape when digits are added.
    // Reuse the fitted font while ticking instead of measuring every centisecond.
    const bool resize=std::strlen(previous)!=std::strlen(value);
    compact::setText(obj,value);
    if(!resize)return;
    const lv_font_t *font=&lv_font_montserrat_8;
    for(auto candidate:{&lv_font_montserrat_14,&lv_font_montserrat_12,&lv_font_montserrat_10,&lv_font_montserrat_8}){
        font=candidate;if(numericWidth(value,font)<=lv_obj_get_style_width(obj,LV_PART_MAIN))break;
    }
    if(lv_obj_get_style_text_font(obj,LV_PART_MAIN)!=font)lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_y(obj,(height-font->line_height)/2);
}
lv_obj_t *lapRow(lv_obj_t *parent,int height,bool live=false) {
    auto row=compact::panel(parent,228,height);lv_obj_set_clickable(row,false);
    lv_obj_set_style_bg_color(row,lv_color_hex(live?LiveBg:LapsBg),0);
    lv_obj_set_style_text_color(row,live?lv_color_white():lv_color_hex(Normal),0);
    text(row,"",6,0,27,&lv_font_montserrat_14);
    auto split=text(row,"",35,0,88,&lv_font_montserrat_14);
    auto total=text(row,"",128,0,94,&lv_font_montserrat_14);
    for(auto obj:{split,total})lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_RIGHT,0);
    return row;
}
void lapText(lv_obj_t *row,uint32_t number,uint64_t splitUs,uint64_t totalUs) {
    // Rows and cells use fixed pixel styles, available before LVGL's first layout.
    char value[40];const int height=lv_obj_get_style_height(row,LV_PART_MAIN);
    std::snprintf(value,sizeof(value),"%02lu",static_cast<unsigned long>(number));
    numericText(lv_obj_get_child(row,0),value,height);
    core::stopwatch::format(value,sizeof(value),splitUs);numericText(lv_obj_get_child(row,1),value,height);
    core::stopwatch::format(value,sizeof(value),totalUs);numericText(lv_obj_get_child(row,2),value,height);
}
}
void Timer::onCreate(lv_obj_t *parent) {
    timeDigits=0;
    lv_obj_set_style_bg_color(parent,theme::bg(),0);
    powerButton=compact::button(parent,"",touch,this,76,40);
    keyButton=compact::button(parent,"",touch,this,76,40);
    lv_obj_set_pos(powerButton,82,4);lv_obj_set_pos(keyButton,160,4);
    powerText=text(powerButton,"复位",0,0,70);lv_obj_center(powerText);
    keyText=text(keyButton,"开始",0,0,70);lv_obj_center(keyText);
    for(auto obj:{powerText,keyText})lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    compact::buttonBackground(keyButton,theme::accent());

    auto back=compact::button(parent,"返回",touch,this,76,40);lv_obj_set_pos(back,4,4);
    lv_obj_set_style_text_font(lv_obj_get_child(back,0),theme::fontMenu(),0);
    auto clock=compact::panel(parent,232,76);lv_obj_set_pos(clock,4,50);
    lv_obj_set_clickable(clock,false);lv_obj_set_style_radius(clock,6,0);
    lv_obj_set_style_bg_color(clock,lv_color_hex(ClockBg),0);
    lv_obj_set_style_text_color(clock,lv_color_white(),0);
    clockGroup=compact::panel(clock,224,48);lv_obj_set_clickable(clockGroup,false);
    lv_obj_set_style_bg_opa(clockGroup,LV_OPA_TRANSP,0);
    lv_obj_set_style_text_color(clockGroup,lv_color_white(),0);
    lv_obj_set_style_transform_pivot_x(clockGroup,0,0);lv_obj_set_style_transform_pivot_y(clockGroup,0,0);
    timeText=text(clockGroup,"00:00",0,0,170,&lv_font_montserrat_48);
    fractionText=text(clockGroup,".00",172,19,50,&lv_font_montserrat_24);
    for(auto obj:{timeText,fractionText})lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_RIGHT,0);
    statusText=text(clock,"",4,27,224);
    lv_obj_set_style_text_color(statusText,theme::danger(),0);
    lv_obj_set_style_text_align(statusText,LV_TEXT_ALIGN_CENTER,0);
    auto laps=compact::panel(parent,232,104);lv_obj_set_pos(laps,4,132);
    lv_obj_set_clickable(laps,false);lv_obj_set_style_radius(laps,6,0);
    lv_obj_set_style_bg_color(laps,lv_color_hex(LapsBg),0);
    liveRow=lapRow(laps,28,true);lv_obj_set_pos(liveRow,2,0);
    // Only saved laps scroll; the live split remains visible above them.
    list=compact::panel(laps,232,73,true);lv_obj_set_pos(list,0,31);
    lv_obj_set_style_bg_color(list,lv_color_hex(LapsBg),0);
    lv_obj_set_style_pad_row(list,0,0);
    lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(list,2,0);
    lv_obj_set_style_bg_color(list,lv_color_hex(Normal),LV_PART_SCROLLBAR);
    timer=lv_timer_create(tick,20,this);lv_timer_pause(timer);
    refresh(esp_timer_get_time());
}
void Timer::onShow() {
    if(visible)return;
    visible=true;closing=false;keySession=sys::Buttons::claimTimerKeys();
    sys::Power::activity();
    if(timer)lv_timer_resume(timer);
}
void Timer::onHide() {
    visible=false;
    if(timer)lv_timer_pause(timer);
    sys::Buttons::releaseTimerKeys(keySession);keySession=0;
    model.pause(esp_timer_get_time());
}
void Timer::onDestroy() {
    onHide();
    if(timer){lv_timer_delete(timer);timer=nullptr;}
    model.reset();
    powerButton=keyButton=powerText=keyText=clockGroup=timeText=fractionText=statusText=nullptr;
    list=liveRow=nullptr;
}
void Timer::exit() {
    closing=true;
    sys::Power::activity();
    ui::WindowManager::instance().goHome();
}
bool Timer::drainKeys() {
    if(!visible || closing)return false;
    sys::TimerKey input;
    while(sys::Buttons::takeTimerKey(keySession,input)) {
        if(input.action==sys::TimerKey::Home){exit();return false;}
        if(input.action==sys::TimerKey::Overflow){inputOverflow=true;continue;}
        act(input.action,input.timeUs);
    }
    return true;
}
void Timer::tick(lv_timer_t *timer) {
    auto self=static_cast<Timer *>(lv_timer_get_user_data(timer));
    if(self->drainKeys())self->refresh(esp_timer_get_time());
}
void Timer::touch(lv_event_t *event) {
    auto self=static_cast<Timer *>(lv_event_get_user_data(event));
    auto target=lv_event_get_target_obj(event);
    if(!self->drainKeys())return;
    sys::Power::activity();
    if(target==self->powerButton)self->act(sys::TimerKey::Power,esp_timer_get_time());
    else if(target==self->keyButton)self->act(sys::TimerKey::Key,esp_timer_get_time());
    else {self->exit();return;}
    self->refresh(esp_timer_get_time());
}
void Timer::act(sys::TimerKey::Action action,int64_t at) {
    inputOverflow=false;
    if(action==sys::TimerKey::Key) {
        if(model.running()){
            const auto count=model.count();model.record(at);if(model.count()!=count)appendLap();
        }
        else model.start(at);
    } else if(action==sys::TimerKey::Power) {
        if(model.running())model.pause(at);
        else {
            model.reset();lv_obj_clean(list);
            lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
        }
    }
}
void Timer::refresh(int64_t now) {
    const auto elapsed=model.elapsed(now);
    char buf[48];core::stopwatch::format(buf,sizeof(buf),elapsed,false);
    compact::setText(timeText,buf);
    // Reserve the widest digits so centisecond updates never shift the layout.
    // Keep the complete group centered when hours add another pair of digits.
    if(timeDigits!=std::strlen(buf)) {
        timeDigits=std::strlen(buf);
        const auto font=&lv_font_montserrat_48,fractionFont=&lv_font_montserrat_24;
        const int mainWidth=numericWidth(buf,font),fractionWidth=numericWidth(".00",fractionFont);
        const int width=mainWidth+2+fractionWidth,height=font->line_height;
        const int scale=std::min(224*256/width,68*256/height);
        lv_obj_set_width(timeText,mainWidth);lv_obj_set_width(fractionText,fractionWidth);
        lv_obj_set_pos(fractionText,mainWidth+2,height-font->base_line-fractionFont->line_height+fractionFont->base_line);
        lv_obj_set_size(clockGroup,width,height);lv_obj_set_style_transform_scale(clockGroup,scale,0);
        lv_obj_set_pos(clockGroup,(232-width*scale/256)/2,(76-height*scale/256)/2);
    }
    std::snprintf(buf,sizeof(buf),".%02u",unsigned(elapsed/10000%100));compact::setText(fractionText,buf);
    const auto count=model.count();
    lapText(liveRow,count==UINT32_MAX?count:count+1,model.split(now),elapsed);
    compact::setText(powerText,model.running()?"暂停":"复位");
    compact::setText(keyText,model.running()?"记录":elapsed?"继续":"开始");
    compact::disabled(powerButton,!model.running() && !elapsed && !model.size());
    compact::setText(statusText,inputOverflow?"按键过快，请重试":"");
    lv_obj_set_hidden(statusText,!inputOverflow);lv_obj_set_hidden(clockGroup,inputOverflow);
}
void Timer::appendLap() {
    if(lv_obj_get_child_count(list)>=core::stopwatch::Model::Capacity)lv_obj_delete(lv_obj_get_child(list,-1));
    const auto &lap=model.newest(0);
    auto row=lapRow(list,24);lapText(row,lap.number,lap.splitUs,lap.totalUs);
    lv_obj_move_to_index(row,0);lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
    colorLaps();
}
void Timer::colorLaps() {
    if(!model.size())return;
    uint64_t shortest=model.newest(0).splitUs,longest=shortest;
    for(size_t i=1;i<model.size();++i){
        shortest=std::min(shortest,model.newest(i).splitUs);longest=std::max(longest,model.newest(i).splitUs);
    }
    // Equal laps (including a single record) have no distinct fastest/slowest.
    for(size_t i=0;i<model.size();++i){
        const auto split=model.newest(i).splitUs;
        const uint32_t color=shortest==longest?Normal:split==shortest?Fast:split==longest?Slow:Normal;
        lv_obj_set_style_text_color(lv_obj_get_child(list,i),lv_color_hex(color),0);
    }
}
}
