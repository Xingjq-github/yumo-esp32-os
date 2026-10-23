#include "touch_scope.hpp"
#include "core/power_mgr.hpp"
#include "core/event_bus.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include <cstdio>
namespace apps {
namespace {
constexpr uint32_t kColors[]={0x45D9FF,0xFFB44D,0x77E29A,0xBA9BFF,0xF37AAA};
}
void TouchScope::onCreate(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent,lv_color_black(),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(parent,0,0);lv_obj_remove_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    // Fixed pool: no new objects, heap allocations or bus reads during sampling.
    for(auto &dot:m_dots) {
        dot.obj=ui::compact::panel(parent,8,8);lv_obj_set_style_radius(dot.obj,LV_RADIUS_CIRCLE,0);
        lv_obj_remove_flag(dot.obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(dot.obj,LV_OBJ_FLAG_HIDDEN);
    }
    for(unsigned i=0;i<5;++i) {
        m_values[i]=ui::compact::label(parent,"",238);lv_obj_set_pos(m_values[i],1,i*23);
        lv_obj_set_style_text_color(m_values[i],lv_color_hex(kColors[i]),0);
        lv_obj_add_flag(m_values[i],LV_OBJ_FLAG_HIDDEN);
    }
    m_screenOffSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) { bsp_touch_observer_enable(false);clear(); });
    m_screenOnSub=sys::EventBus::subscribe(sys::EventType::ScreenOn,[this](const sys::Event &) {
        if(m_visible) { m_lastSequence=0;bsp_touch_observer_enable(true); }
    });
    m_timer=lv_timer_create(timerCb,25,this);
}
void TouchScope::clear() {
    for(auto &dot:m_dots) { if(dot.active) lv_obj_add_flag(dot.obj,LV_OBJ_FLAG_HIDDEN);dot.active=false; }
    for(auto label:m_values) lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
    m_textVisible=m_down=false;m_lastError=ESP_OK;m_next=0;
}
void TouchScope::onShow() {
    clear();m_visible=true;m_lastSequence=0;bsp_touch_observer_enable(sys::Power::isScreenOn());
}
void TouchScope::onHide() {
    m_visible=false;bsp_touch_observer_enable(false);clear();
}
void TouchScope::onDestroy() {
    onHide();sys::EventBus::unsubscribe(m_screenOffSub);sys::EventBus::unsubscribe(m_screenOnSub);
    if(m_timer) { lv_timer_delete(m_timer);m_timer=nullptr; }
}
void TouchScope::timerCb(lv_timer_t *t) { static_cast<TouchScope *>(lv_timer_get_user_data(t))->update(); }
void TouchScope::present(const bsp_touch_snapshot_t &sample,uint32_t now) {
    char text[64];
    if(sample.coordinates_valid) {
        std::snprintf(text,sizeof(text),"X  %u",unsigned(sample.x));ui::compact::setText(m_values[0],text);
        std::snprintf(text,sizeof(text),"Y  %u",unsigned(sample.y));ui::compact::setText(m_values[1],text);
        // Board currently has no swap/mirror. Keep chip coordinates unmodified
        // in the labels; clamp only the visual trail to the 240x240 display.
        auto &dot=m_dots[m_next];
        const int x=sample.x>235?232:sample.x<4?0:sample.x-4;
        const int y=sample.y>235?232:sample.y<4?0:sample.y-4;
        lv_obj_set_pos(dot.obj,x,y);lv_obj_set_style_bg_color(dot.obj,lv_color_hex(kColors[m_next%5]),0);
        lv_obj_remove_flag(dot.obj,LV_OBJ_FLAG_HIDDEN);dot.at=now;dot.active=true;
        m_next=(m_next+1)%TrailCount;
    } else {
        ui::compact::setText(m_values[0],"X  --");ui::compact::setText(m_values[1],"Y  --");
    }
    if(sample.error==ESP_OK) std::snprintf(text,sizeof(text),"N  %u",unsigned(sample.points));
    else std::snprintf(text,sizeof(text),"N  --");
    ui::compact::setText(m_values[2],text);
    std::snprintf(text,sizeof(text),"t  %lu ms",static_cast<unsigned long>(sample.timestamp_ms));ui::compact::setText(m_values[3],text);
    if(sample.error!=ESP_OK) std::snprintf(text,sizeof(text),"I2C  0x%X",unsigned(sample.error));
    else std::snprintf(text,sizeof(text),"#  %lu",static_cast<unsigned long>(sample.sequence));
    ui::compact::setText(m_values[4],text);
    lv_obj_set_style_text_color(m_values[4],sample.error==ESP_OK?lv_color_hex(kColors[4]):ui::theme::danger(),0);
    for(auto label:m_values) lv_obj_remove_flag(label,LV_OBJ_FLAG_HIDDEN);
    m_lastActivity=now;m_textVisible=true;
}
void TouchScope::update() {
    if(!m_visible || !sys::Power::isScreenOn()) return;
    const auto now=lv_tick_get();
    bsp_touch_snapshot_t sample{};
    if(bsp_touch_observer_snapshot(&sample) && sample.sequence!=m_lastSequence) {
        m_lastSequence=sample.sequence;
        // Idle zero-point polls do not prolong the display: black again in 2s.
        if(sample.coordinates_valid || m_down || sample.error!=m_lastError) present(sample,now);
        m_down=sample.coordinates_valid;m_lastError=sample.error;
    }
    for(auto &dot:m_dots) if(dot.active && uint32_t(now-dot.at)>=LifetimeMs) {
        lv_obj_add_flag(dot.obj,LV_OBJ_FLAG_HIDDEN);dot.active=false;
    }
    if(m_textVisible && uint32_t(now-m_lastActivity)>=LifetimeMs) {
        for(auto label:m_values) lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
        m_textVisible=false;
    }
}
} // namespace apps
