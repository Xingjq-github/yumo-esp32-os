#include "flashlight.hpp"
#include "flashlight_signal.hpp"
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/sysinfo_overlay.hpp"
#include "core/power_mgr.hpp"
#include "core/event_bus.hpp"
#include <algorithm>
#include <cstdlib>
namespace apps {
void Flashlight::onCreate(lv_obj_t *parent) {
    lv_obj_set_scrollable(parent,false);
    lv_obj_set_clickable(parent,true);
    lv_obj_add_event_cb(parent,touchCb,LV_EVENT_ALL,this);
    m_sosButton=ui::compact::button(parent,"sos",sosCb,this,52,36);
    lv_obj_align(m_sosButton,LV_ALIGN_TOP_RIGHT,-1,1);
    lv_obj_set_gesture_bubble(m_sosButton,false);
    m_sosLabel=lv_obj_get_child(m_sosButton,0);
    m_levelLabel=ui::compact::label(parent,""); lv_obj_align(m_levelLabel,LV_ALIGN_BOTTOM_MID,0,-1);
    m_hint=ui::compact::label(parent,"上滑增亮  下滑减亮\n单击屏幕开关",238);
    lv_label_set_long_mode(m_hint,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(m_hint,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(m_hint);
    m_timer=lv_timer_create(timerCb,20,this); lv_timer_pause(m_timer);
    m_screenOffSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) { stop(); });
}
void Flashlight::onShow() {
    m_level=2;m_pressLevel=2;m_on=true;setSos(false);
    m_visible=true; m_restoreInfo=ui::SysInfoOverlay::visible(); ui::SysInfoOverlay::setVisible(false);
    m_painted=false; paint();
}
void Flashlight::onHide() {
    stop(); m_visible=false; sys::Power::clearTransientBrightness();
    ui::SysInfoOverlay::setVisible(m_restoreInfo);
}
void Flashlight::onDestroy() {
    if(m_screenOffSub) { sys::EventBus::unsubscribe(m_screenOffSub); m_screenOffSub=0; }
    if(m_timer) { lv_timer_delete(m_timer); m_timer=nullptr; }
    sys::Power::clearTransientBrightness();
}
void Flashlight::stop() {
    setSos(false); m_on=false;
    if(m_timer) lv_timer_pause(m_timer);
}
void Flashlight::setSos(bool enabled) {
    m_sos=enabled; m_painted=false;
    lv_obj_set_style_bg_color(m_sosButton,enabled?lv_color_hex(0xC52D44):lv_color_hex(0x30343B),0);
    lv_obj_set_style_text_color(m_sosLabel,enabled?lv_color_white():lv_color_hex(0xAAAAAA),0);
    if(enabled) { m_on=true; m_signalStarted=lv_tick_get(); lv_timer_set_period(m_timer,torch::kUnitMs);lv_timer_reset(m_timer);lv_timer_resume(m_timer); }
    else if(m_timer) lv_timer_pause(m_timer);
}
void Flashlight::paint() {
    if(!m_visible || !sys::Power::isScreenOn()) return;
    const bool lit=m_sos?torch::litAt(lv_tick_get()-m_signalStarted):m_on;
    if(!m_painted || lit!=m_lit || m_drawnLevel!=m_level) {
        m_lit=lit; m_drawnLevel=m_level; m_painted=true;
        lv_obj_set_style_bg_color(root(),lit?lv_color_white():lv_color_black(),0);
        // SOS dark intervals use zero PWM. Touch and the stop target stay active.
        sys::Power::setTransientBrightness(lit?torch::kLevels[m_level]:m_sos?0:5);
        lv_label_set_text_fmt(m_levelLabel,"%d%%  %d/4",torch::kLevels[m_level],m_level+1);
        lv_obj_set_style_text_color(m_levelLabel,lit?lv_color_hex(0x666666):lv_color_hex(0xAAAAAA),0);
        if(m_on || m_sos) lv_obj_set_hidden(m_hint,true);
        else lv_obj_set_hidden(m_hint,false);
        lv_obj_set_style_text_color(m_hint,lv_color_hex(0xAAAAAA),0);
        lv_obj_set_style_bg_color(m_sosButton,m_sos?lv_color_hex(0xC52D44):lv_color_hex(0x30343B),0);
        lv_obj_set_style_text_color(m_sosLabel,m_sos?lv_color_white():lv_color_hex(0xAAAAAA),0);
    }
}
void Flashlight::touchCb(lv_event_t *e) {
    auto self=static_cast<Flashlight *>(lv_event_get_user_data(e));
    auto indev=lv_indev_active(); if(!indev || !sys::Power::isScreenOn()) return;
    const auto code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED) {
        lv_indev_get_point(indev,&self->m_press); self->m_pressLevel=self->m_level; self->m_moved=false;
    } else if(code==LV_EVENT_PRESSING) {
        lv_point_t point; lv_indev_get_point(indev,&point);
        const int dy=self->m_press.y-point.y,dx=point.x-self->m_press.x;
        if(std::abs(dx)>=18 || std::abs(dy)>=18) self->m_moved=true;
        if(std::abs(dy)>=24 && std::abs(dy)>std::abs(dx)) {
            const int steps=1+(std::abs(dy)-24)/40;
            self->m_level=std::clamp(self->m_pressLevel+(dy>0?steps:-steps),0,3);
            self->paint();
        }
    } else if(code==LV_EVENT_GESTURE) self->m_moved=true;
    else if(code==LV_EVENT_CLICKED && !self->m_moved) {
        if(self->m_sos) { self->setSos(false); self->m_on=false; }
        else self->m_on=!self->m_on;
        self->paint();
    }
}
void Flashlight::sosCb(lv_event_t *e) {
    auto self=static_cast<Flashlight *>(lv_event_get_user_data(e));
    if(!sys::Power::isScreenOn()) return;
    const bool enable=!self->m_sos;
    self->setSos(enable);
    if(!enable) self->m_on=false;
    self->paint();
}
void Flashlight::timerCb(lv_timer_t *timer) {
    auto self=static_cast<Flashlight *>(lv_timer_get_user_data(timer));
    if(!sys::Power::isScreenOn()) { self->stop(); return; }
    self->paint(); // derive current phase from time; never burst through missed edges
    lv_timer_set_period(timer,torch::untilEdge(lv_tick_get()-self->m_signalStarted));
}
} // namespace apps
