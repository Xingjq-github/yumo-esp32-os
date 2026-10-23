#include "lan.hpp"
#include "core/power_mgr.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace apps {
namespace {
namespace c=ui::compact;
constexpr char Help[]="";
lv_obj_t *input(lv_obj_t *parent,const char *title,const std::string &value,unsigned maxLength) {
    // Use the SAME system field binding and layout constants as Wi-Fi settings.
    constexpr int height=ui::TextInputLayout::NetworkFieldHeight;
    auto row=c::panel(parent,LV_PCT(100),height);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    c::label(row,title,44);
    auto field=c::networkField(row,title,value.c_str(),maxLength);
    lv_obj_set_width(field,195);
    lv_obj_set_style_border_color(field,ui::theme::accent(),LV_STATE_FOCUSED);
    lv_textarea_set_password_mode(field,false);
    return field;
}
void small(lv_obj_t *label) { lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0); }
void formatRate(char *out,size_t size,uint32_t rate,bool available) {
    if(!available) std::snprintf(out,size,"--");
    else if(rate>=1000000) std::snprintf(out,size,"%.2f MB/s",double(rate)/1000000);
    else if(rate>=1000) std::snprintf(out,size,"%.1f KB/s",double(rate)/1000);
    else std::snprintf(out,size,"%u B/s",static_cast<unsigned>(rate));
}
}
void Lan::onCreate(lv_obj_t *parent) {
    // WindowManager already hides its status bar for Apps. No local header/back
    // bar: the entire 240x240 viewport belongs to one vertical scroll container.
    body=c::panel(parent,240,240,true);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body,LV_SCROLLBAR_MODE_ACTIVE);
    toggle=c::toggle(body,"局域网热点",false,toggleCb,this);
    status=c::label(body,"热点已关闭 · 最多4台",LV_PCT(100));
    lv_obj_set_height(status,38); lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_WRAP);
    ssid=input(body,"名称",sys::Settings::lanSsid(),32);
    password=input(body,"密码",sys::Settings::lanPassword(),63);
    lv_obj_add_event_cb(ssid,fieldCb,LV_EVENT_DEFOCUSED,this);
    lv_obj_add_event_cb(password,fieldCb,LV_EVENT_DEFOCUSED,this);
    hint=c::label(body,Help,LV_PCT(100)); lv_label_set_long_mode(hint,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(hint,true);
    lv_obj_set_style_text_color(hint,ui::theme::muted(),0);
    heading=c::section(body,"连接设备 0/4");
    empty=c::label(body,"开启热点后，等待设备连接",LV_PCT(100));
    lv_obj_set_style_min_height(empty,36,0); lv_label_set_long_mode(empty,LV_LABEL_LONG_MODE_WRAP);
    for(auto &row:rows) {
        row.panel=c::panel(body,LV_PCT(100),sys::lan::ClientHeight);
        lv_obj_set_style_bg_color(row.panel,ui::theme::surface(),0);
        lv_obj_set_style_radius(row.panel,5,0); lv_obj_set_style_pad_all(row.panel,4,0);
        row.name=c::label(row.panel,"",228); lv_obj_set_pos(row.name,0,0);
        row.mac=c::label(row.panel,"",228); small(row.mac); lv_obj_set_pos(row.mac,0,22);
        row.ip=c::label(row.panel,"",228); small(row.ip); lv_obj_set_pos(row.ip,0,38);
        row.upload=c::label(row.panel,"",114); lv_obj_set_pos(row.upload,0,55);
        row.download=c::label(row.panel,"",114); lv_obj_set_pos(row.download,114,55);
        lv_obj_set_style_text_color(row.upload,ui::theme::accent(),0);
        lv_obj_set_style_text_color(row.download,ui::theme::success(),0);
        lv_obj_set_hidden(row.panel,true);
    }
    auto note=c::label(body,"浏览器：SD文件 · 密码查看\n退出或深睡后关闭服务。",LV_PCT(100));
    lv_label_set_long_mode(note,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(note,ui::theme::muted(),0);
}
void Lan::onShow() {
    if(!session) session=sys::LanHotspot::open();
    if(!timer) timer=lv_timer_create(timerCb,500,this);
    refresh();
}
void Lan::onHide() {
    // RAM-only cache update; the existing service batches any NVS commit.
    if(!fieldsLocked) (void)saveFields(false);
    if(session) { sys::LanHotspot::close(session); session=0; }
    if(timer) { lv_timer_delete(timer); timer=nullptr; }
    ui::TextInput::hide();
}
void Lan::onDestroy() { onHide(); body=toggle=status=ssid=password=hint=heading=empty=nullptr; rows={}; }
bool Lan::onBack() {
    if(ui::TextInput::visible()) { ui::TextInput::hide(); return true; }
    return false;
}
bool Lan::saveFields(bool report) {
    if(!ssid || !password) return false;
    const std::string name=lv_textarea_get_text(ssid),pass=lv_textarea_get_text(password);
    const auto error=sys::lan::validate(name,pass);
    if(error==sys::lan::ConfigError::None) {
        (void)sys::Settings::setLanCredentials(name,pass);
        if(report) { c::setText(hint,Help); lv_obj_set_hidden(hint,true); }
        return true;
    }
    if(report) {
        lv_obj_set_hidden(hint,false);
        c::setText(hint,error==sys::lan::ConfigError::Name?"名称需1~32字节，不能全为空格":"密码需8~63位，支持英文、数字和符号");
        lv_obj_set_style_text_color(hint,ui::theme::danger(),0);
        lv_obj_scroll_to_view(hint,LV_ANIM_OFF);
    }
    return false;
}
void Lan::toggleCb(lv_event_t *event) {
    auto self=static_cast<Lan *>(lv_event_get_user_data(event));
    const auto state=sys::LanHotspot::snapshot();
    if(state.busy()) { self->refresh(); return; }
    ui::TextInput::hide();
    if(state.phase==sys::LanPhase::On) sys::LanHotspot::disable(self->session);
    else {
        if(!self->saveFields(true)) { c::checked(self->toggle,false); return; }
        const auto error=sys::LanHotspot::enable(self->session,lv_textarea_get_text(self->ssid),lv_textarea_get_text(self->password));
        if(error!=ESP_OK) {
            lv_obj_set_hidden(self->hint,false);
            const auto *message=sys::SystemService::errorText(error);
            c::setText(self->hint,message?message:"热点启动失败，请重试");
            lv_obj_set_style_text_color(self->hint,ui::theme::danger(),0);
        }
    }
    self->refresh();
}
void Lan::fieldCb(lv_event_t *event) {
    auto self=static_cast<Lan *>(lv_event_get_user_data(event));
    if(!self->fieldsLocked) (void)self->saveFields(false);
}
void Lan::timerCb(lv_timer_t *timer) {
    auto self=static_cast<Lan *>(lv_timer_get_user_data(timer));
    if(sys::Power::isScreenOn()) self->refresh();
}
void Lan::refresh() {
    const auto state=sys::LanHotspot::snapshot();
    fieldsLocked=state.busy() || state.phase==sys::LanPhase::On;
    c::disabled(ssid,fieldsLocked); c::disabled(password,fieldsLocked);
    c::disabled(toggle,state.busy() || !timer || sys::Power::sleepPending());
    c::checked(toggle,state.wanted && state.phase!=sys::LanPhase::Failed);
    char text[160];
    switch(state.phase) {
    case sys::LanPhase::Off: std::snprintf(text,sizeof(text),"热点已关闭 · 最多4台"); break;
    case sys::LanPhase::Preparing: std::snprintf(text,sizeof(text),"正在准备热点…\n自动检查上游 Wi-Fi"); break;
    case sys::LanPhase::Starting: std::snprintf(text,sizeof(text),"正在开启热点…"); break;
    case sys::LanPhase::Stopping: std::snprintf(text,sizeof(text),"正在关闭热点…\n%s",state.error==ESP_OK?"正在释放无线资源":"等待无线停止，请稍候"); break;
    case sys::LanPhase::On:
        std::snprintf(text,sizeof(text),"%s · %u/4台\n%s%s",state.sharing?"共享当前 Wi-Fi":state.upstreamConnected?"共享重试中":"本地局域网",state.count,
          state.webReady?"http://":state.webError!=ESP_OK?"网页启动失败 ":"网页准备中 ",state.ip[0]?state.ip:"--");break;
    case sys::LanPhase::Failed: {
        const auto *error=sys::SystemService::errorText(state.error);
        std::snprintf(text,sizeof(text),"%s\n错误 0x%x",error?error:"热点开启失败，请重试",static_cast<unsigned>(state.error)); break;
    }
    }
    c::setText(status,text);
    lv_obj_set_height(status,state.phase==sys::LanPhase::Failed?LV_SIZE_CONTENT:state.phase==sys::LanPhase::Off?20:38);
    lv_obj_set_style_text_color(status,state.phase==sys::LanPhase::Failed?ui::theme::danger():state.phase==sys::LanPhase::On?ui::theme::accent():ui::theme::fg(),0);
    std::snprintf(text,sizeof(text),"连接设备 %u/4",state.count); c::setText(heading,text);
    if(state.count) lv_obj_set_hidden(empty,true);
    else {
        lv_obj_set_hidden(empty,false);
        c::setText(empty,state.phase==sys::LanPhase::On?(state.sharing?"等待设备连接，已启用网络共享":state.upstreamConnected?"上游已连接，正在配置网络共享":"等待连接\n无上游Wi-Fi，可用于本地组网"):"开启热点后，等待设备连接");
    }
    for(unsigned i=0;i<rows.size();++i) {
        auto &row=rows[i];
        if(i>=state.count) { lv_obj_set_hidden(row.panel,true); continue; }
        const auto &peer=state.clients[i]; const auto *mac=peer.mac;
        lv_obj_set_hidden(row.panel,false);
        // SoftAP association does not provide the phone/computer's display name.
        // Use a stable MAC suffix instead of inventing names from vendor prefixes.
        std::snprintf(text,sizeof(text),"设备 %02X%02X  %d dBm",mac[4],mac[5],peer.rssi); c::setText(row.name,text);
        std::snprintf(text,sizeof(text),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]); c::setText(row.mac,text);
        std::snprintf(text,sizeof(text),"IP %s",peer.ip[0]?peer.ip:"-- (DHCP)"); c::setText(row.ip,text);
        char rate[24]; formatRate(rate,sizeof(rate),peer.uploadBps,state.uploadAvailable);
        std::snprintf(text,sizeof(text),LV_SYMBOL_UPLOAD " %s",rate); c::setText(row.upload,text);
        formatRate(rate,sizeof(rate),peer.downloadBps,state.downloadAvailable);
        std::snprintf(text,sizeof(text),LV_SYMBOL_DOWNLOAD " %s",rate); c::setText(row.download,text);
    }
}
} // namespace apps
