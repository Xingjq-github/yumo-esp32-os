#include "bt_music.hpp"
#include "core/ble_hid.hpp"
#include "core/system_service.hpp"
#include "core/power_mgr.hpp"
#include "ui/hid_controls.hpp"
namespace apps {
namespace {
struct MediaDef { const char *label; sys::ConsumerKey key; };
constexpr MediaDef kMediaKeys[]={
    {LV_SYMBOL_PREV,sys::ConsumerKey::PreviousTrack},
    {LV_SYMBOL_PLAY "  " LV_SYMBOL_PAUSE,sys::ConsumerKey::PlayPause},
    {LV_SYMBOL_NEXT,sys::ConsumerKey::NextTrack},
    {LV_SYMBOL_VOLUME_MID " -",sys::ConsumerKey::VolumeDown},
    {LV_SYMBOL_MUTE,sys::ConsumerKey::Mute},
    {LV_SYMBOL_VOLUME_MAX " +",sys::ConsumerKey::VolumeUp},
};
}
void BtMusic::onCreate(lv_obj_t *parent) {
    namespace c=ui::compact;
    lv_obj_set_style_bg_color(parent,ui::theme::bg(),0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_set_style_pad_all(parent,0,0);
    lv_obj_set_scrollable(parent,false);
    auto album=c::panel(parent,240,138);lv_obj_set_pos(album,0,0);
    lv_obj_set_style_bg_color(album,ui::theme::surface(),0);lv_obj_set_style_radius(album,ui::theme::radius(),0);
    auto disc=c::panel(album,128,128);lv_obj_center(disc);
    lv_obj_set_style_radius(disc,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(disc,ui::theme::bg(),0);
    lv_obj_set_style_border_width(disc,2,0);lv_obj_set_style_border_color(disc,ui::theme::edge(),0);
    auto ring=c::panel(disc,96,96);lv_obj_center(ring);
    lv_obj_set_style_radius(ring,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(ring,ui::theme::surface(),0);
    lv_obj_set_style_border_width(ring,1,0);lv_obj_set_style_border_color(ring,ui::theme::accent(),0);
    auto note=c::label(ring,LV_SYMBOL_AUDIO);lv_obj_set_style_text_font(note,ui::theme::fontLarge(),0);
    lv_obj_set_style_text_color(note,ui::theme::accent(),0);lv_obj_center(note);
    m_link=c::label(album,LV_SYMBOL_BLUETOOTH);lv_obj_align(m_link,LV_ALIGN_TOP_RIGHT,-3,2);
    // HID is a media remote: intentionally no invented title, progress or playing state.
    for(unsigned i=0;i<6;++i) {
        m_buttons[i]=ui::hid::button(parent,kMediaKeys[i].label,actionCb,this,(i%3)*80,i<3?139:198,79,i<3?58:42);
        lv_obj_set_user_data(m_buttons[i],const_cast<MediaDef *>(&kMediaKeys[i]));
        if(i<3) lv_obj_set_style_text_font(ui::hid::caption(m_buttons[i]),ui::theme::fontLarge(),0);
    }
    m_statusTimer=lv_timer_create(statusTimerCb,250,this);
}
void BtMusic::onShow() { m_visible=true;if(m_statusTimer) lv_timer_resume(m_statusTimer);refreshStatus(); }
void BtMusic::onHide() { m_visible=false;if(m_statusTimer) lv_timer_pause(m_statusTimer); }
void BtMusic::onDestroy() {
    onHide();if(m_statusTimer) { lv_timer_delete(m_statusTimer);m_statusTimer=nullptr; }
}
void BtMusic::actionCb(lv_event_t *e) {
    auto self=static_cast<BtMusic *>(lv_event_get_user_data(e));
    if(!self->m_visible || !sys::Power::isScreenOn()) return;
    auto item=static_cast<MediaDef *>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(item && sys::SystemService::sendConsumer(item->key)!=ESP_OK) {
        self->m_errorUntil=lv_tick_get()+1000;self->refreshStatus();
    }
}
void BtMusic::statusTimerCb(lv_timer_t *timer) { static_cast<BtMusic *>(lv_timer_get_user_data(timer))->refreshStatus(); }
void BtMusic::refreshStatus() {
    if(!m_visible || !sys::Power::isScreenOn()) return;
    const auto state=sys::SystemService::bluetooth();
    const bool connected=sys::BleHid::remoteConnected();
    if(m_errorUntil && static_cast<int32_t>(lv_tick_get()-m_errorUntil)>=0) m_errorUntil=0;
    const bool error=state.hasError() || (m_errorUntil && static_cast<int32_t>(m_errorUntil-lv_tick_get())>0);
    // Match Settings/system status: gray idle, amber transition, green linked.
    const bool pending=state.enabled && (state.state==sys::BleHidState::Starting || state.state==sys::BleHidState::Connecting);
    const int rail=error?3:state.peer.connected?2:pending?1:0;
    if(rail!=m_railState) {
        m_railState=rail;
        lv_obj_set_style_text_color(m_link,error?ui::theme::danger():state.peer.connected?ui::theme::success():pending?ui::theme::warning():ui::theme::muted(),0);
    }
    for(auto button:m_buttons) ui::compact::disabled(button,!connected);
}
} // namespace apps
