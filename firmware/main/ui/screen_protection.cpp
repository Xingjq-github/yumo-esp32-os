#include "screen_protection.hpp"
#include "screen_protection_model.hpp"
#include "core/power_mgr.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ui {
namespace {
lv_indev_t *pointer=nullptr;
lv_indev_read_cb_t originalRead=nullptr;
lv_obj_t *cover=nullptr,*clockLabel=nullptr;
protection::Idle idle;
protection::TouchGate touch;
bool dismiss=false;
uint32_t enteredAt=0,paintAt=0;
int lastX=-1,lastY=-1;

void readPointer(lv_indev_t *input,lv_indev_data_t *data) {
    originalRead(input,data);
    if(touch.filter(cover!=nullptr,data->state==LV_INDEV_STATE_PRESSED)) {
        dismiss=true;
        data->state=LV_INDEV_STATE_RELEASED;
        data->continue_reading=false;
    }
}
void close() {
    if(cover)lv_obj_delete(cover);
    cover=clockLabel=nullptr;
}
bool open(uint32_t now) {
    cover=lv_obj_create(lv_layer_top());
    if(!cover)return false;
    lv_obj_remove_style_all(cover);
    lv_obj_set_pos(cover,0,0);lv_obj_set_size(cover,240,240);
    lv_obj_set_clickable(cover,false);lv_obj_set_scrollable(cover,false);
    // Opaque neutral field replaces fixed graphics instead of darkening them.
    lv_obj_set_style_bg_color(cover,lv_color_hex(0x202020),0);
    lv_obj_set_style_bg_opa(cover,LV_OPA_COVER,0);
    clockLabel=lv_label_create(cover);
    if(!clockLabel) {close();return false;}
    lv_obj_remove_style_all(clockLabel);
    lv_obj_set_clickable(clockLabel,false);
    lv_obj_set_size(clockLabel,96,36);
    lv_obj_set_style_text_font(clockLabel,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_color(clockLabel,lv_color_hex(0x686868),0);
    lv_obj_set_style_text_align(clockLabel,LV_TEXT_ALIGN_CENTER,0);
    enteredAt=now;paintAt=now-1000;lastX=lastY=-1;
    return true;
}
void paint(uint32_t now) {
    if(uint32_t(now-paintAt)<1000)return;
    paintAt=now;
    const time_t current=time(nullptr);
    struct tm local{};
    char text[12];
    if(localtime_r(&current,&local))std::snprintf(text,sizeof(text),"%02d:%02d",local.tm_hour,local.tm_min);
    else std::snprintf(text,sizeof(text),"--:--");
    if(std::strcmp(lv_label_get_text(clockLabel),text)!=0)lv_label_set_text(clockLabel,text);
    const auto pos=protection::position(uint32_t(now-enteredAt));
    if(pos.x!=lastX || pos.y!=lastY) {
        lastX=pos.x;lastY=pos.y;lv_obj_set_pos(clockLabel,pos.x,pos.y);
    }
}
}
void ScreenProtection::init(lv_indev_t *input) {
    if(pointer || !input)return;
    originalRead=lv_indev_get_read_cb(input);
    if(!originalRead)return;
    pointer=input;lv_indev_set_read_cb(pointer,readPointer);
}
void ScreenProtection::reset() {close();idle.reset();dismiss=false;}
bool ScreenProtection::visible() {return cover!=nullptr;}
void ScreenProtection::poll(bool allowed) {
    // No input device means no reliable touch dismissal: do not cover the UI.
    allowed=allowed && pointer && sys::Power::isScreenOn() && !sys::Power::sleepPending();
    if(dismiss) {reset();}
    const uint32_t now=lv_tick_get();
    if(!idle.step(now,sys::Power::activityGeneration(),allowed)) {close();return;}
    if(!cover && !open(now)) {idle.reset();return;}
    paint(now);
}
}
