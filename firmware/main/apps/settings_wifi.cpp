#include "settings.hpp"
#include "ui/compact_ui.hpp"
#include "theme.hpp"
#include "core/settings_store.hpp"
#include "esp_wifi.h"
#include <cstdio>
#include <cstring>
namespace apps {
namespace c=ui::compact;
namespace {
const char *wifiError(int reason,sys::WifiConnectPhase phase) {
    if(reason==ESP_ERR_NO_MEM) return "Wi-Fi 内存不足";
    if(reason==WIFI_REASON_AUTH_EXPIRE)
        return phase==sys::WifiConnectPhase::Joining?"热点认证超时，请重试":"热点认证已失效，请重连";
    if(reason==WIFI_REASON_AUTH_FAIL) return "认证失败，请检查密码";
    if(reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason==WIFI_REASON_HANDSHAKE_TIMEOUT) return "握手超时，请重试";
    if(reason==sys::Connectivity::NetworkNotSeen) return "扫描未发现目标网络";
    if(reason==WIFI_REASON_NO_AP_FOUND) return "连接时未找到目标网络";
    if(reason==WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY)
        return phase==sys::WifiConnectPhase::Joining?"热点连接超时或被断开":"热点断开了连接";
    if(reason==WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY || reason==WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD) return "网络加密方式不兼容";
    if(reason==WIFI_REASON_ASSOC_FAIL) return "路由器拒绝连接";
    if(reason==WIFI_REASON_BEACON_TIMEOUT || reason==WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD) return "信号不稳定，请靠近路由器";
    if(reason==ESP_ERR_WIFI_STATE) return "网络忙，请重新开启 Wi-Fi";
    if(reason==ESP_ERR_TIMEOUT) return phase==sys::WifiConnectPhase::Address?"获取 IP 超时，请重试":"连接超时，请重试";
    return "连接失败，请重试";
}
}
void Settings::buildWifi() {
    auto state=sys::Connectivity::snapshot();
    v.toggle=c::toggle(v.body,"Wi-Fi",state.enabled,wifiToggleCb,this);
    auto actions=c::panel(v.body,240,36); lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW);
    v.scanButton=c::button(actions,"扫描",wifiScanCb,this,79,36);
    auto saved=c::button(actions,"已保存",pageCb,this,79,36);
    lv_obj_set_user_data(saved,reinterpret_cast<void *>(static_cast<intptr_t>(Page::WifiSaved)));
    auto manual=c::button(actions,"手动",[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        self->manualWifiEntry=true;
        self->selectedSsid.clear(); self->selectedPassword.clear(); self->navigate(Page::WifiNetwork);
    },this,80,36);
    (void)manual;
    v.list=c::panel(v.body,240,LV_SIZE_CONTENT); lv_obj_set_flex_flow(v.list,LV_FLEX_FLOW_COLUMN);
    refreshWifiList();
    v.status=c::label(v.body,"",238); lv_obj_set_height(v.status,40); lv_label_set_long_mode(v.status,LV_LABEL_LONG_MODE_WRAP);
    if(state.enabled && !state.transitioning && state.scan==sys::WifiScanState::Idle && state.state!=sys::WifiState::Searching && state.state!=sys::WifiState::Connecting)
        request(sys::ServiceAction::WifiScan);
}
void Settings::wifiToggleCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    auto toggle=lv_event_get_target_obj(e);
    c::disabled(toggle,true); // close the gap before the next status refresh
    self->request(sys::ServiceAction::WifiEnable,lv_obj_has_state(toggle,LV_STATE_CHECKED));
}
void Settings::wifiScanCb(lv_event_t *e) { static_cast<Settings *>(lv_event_get_user_data(e))->request(sys::ServiceAction::WifiScan); }
void Settings::refreshWifiList() {
    if(!v.list || listPressed) return;
    auto state=sys::Connectivity::snapshot();
    lv_obj_clean(v.list); networkCount=0;
    if(state.enabled) {
        auto count=sys::Connectivity::scanResults(aps.data(),aps.size());
        for(uint16_t i=0;i<count;++i) {
            auto ssid=std::string(reinterpret_cast<char *>(aps[i].ssid),strnlen(reinterpret_cast<char *>(aps[i].ssid),32));
            if(ssid.empty()) continue;
            networkNames[networkCount]=ssid;
            auto row=c::button(v.list,"",networkCb,this);
            lv_obj_set_user_data(row,reinterpret_cast<void *>(networkCount));
            auto name=c::label(row,ssid.c_str(),164); lv_obj_align(name,LV_ALIGN_LEFT_MID,0,0);
            char detail[32];
            if(state.state==sys::WifiState::Connected && state.ssid==ssid) std::snprintf(detail,sizeof(detail),"已连接");
            else if(sys::Settings::wifiKnown(ssid)) std::snprintf(detail,sizeof(detail),"已保存");
            else std::snprintf(detail,sizeof(detail),"%d dBm",aps[i].rssi);
            auto status=c::label(row,detail,70); lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_RIGHT,0);
            lv_obj_set_style_text_color(status,ui::theme::muted(),0); lv_obj_align(status,LV_ALIGN_RIGHT_MID,0,0);
            markListTouch(row); ++networkCount;
        }
    }
    if(!networkCount) {
        auto text=c::label(v.list,!state.enabled?"打开 Wi-Fi 后扫描网络":state.scan==sys::WifiScanState::Scanning?"正在寻找网络…":"暂无网络，点击扫描重试",238);
        lv_label_set_long_mode(text,LV_LABEL_LONG_MODE_WRAP);
    }
    scanRevision=state.scanRevision;
}
void Settings::buildSavedWifi() {
    v.list=c::panel(v.body,240,LV_SIZE_CONTENT); lv_obj_set_flex_flow(v.list,LV_FLEX_FLOW_COLUMN);
    networkCount=0;
    for(const auto &entry:sys::Settings::wifiHistory()) {
        if(networkCount>=networkNames.size()) break;
        networkNames[networkCount]=entry.ssid;
        auto row=c::row(v.list,entry.ssid.c_str(),nullptr,networkCb,this);
        lv_obj_set_user_data(row,reinterpret_cast<void *>(networkCount++)); markListTouch(row);
    }
    if(!networkCount) c::label(v.list,"还没有已保存的网络",238);
    c::section(v.body,"选择网络查看、连接或忘记");
}
void Settings::networkCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(index>=self->networkCount) return;
    self->manualWifiEntry=false;
    self->selectedSsid=self->networkNames[index];
    self->selectedPassword=sys::Settings::wifiPasswordFor(self->selectedSsid);
    self->navigate(Page::WifiNetwork);
}
void Settings::buildNetwork() {
    if(manualWifiEntry) {
        v.ssid=c::networkField(v.body,"网络名称 / SSID",selectedSsid.c_str(),32);
    } else {
        // Selected scan/saved identities are display-only. Keep v.ssid null so
        // connect, forget and status refresh use the original selected value.
        auto name=c::label(v.body,selectedSsid.c_str(),238);
        lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_pad_top(name,6,0);
        lv_obj_set_style_pad_bottom(name,6,0);
    }
    v.password=c::networkField(v.body,"密码（开放网络留空）",selectedPassword.c_str(),64);
    lv_textarea_set_password_mode(v.password,false); // explicit project preference
    v.connect=c::button(v.body,"连接",connectCb,this,LV_PCT(100),36);
    c::buttonBackground(v.connect,ui::theme::accent());
    v.forget=c::button(v.body,"忘记此网络",forgetWifiCb,this,LV_PCT(100),36);
    v.status=c::label(v.body,"",238); lv_label_set_long_mode(v.status,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(v.status,46);
}
void Settings::connectCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    auto state=sys::Connectivity::snapshot();
    if(self->v.ssid) self->selectedSsid=lv_textarea_get_text(self->v.ssid);
    self->selectedPassword=lv_textarea_get_text(self->v.password);
    if(state.state==sys::WifiState::Connected && state.ssid==self->selectedSsid) {
        self->request(sys::ServiceAction::WifiDisconnect); return;
    }
    if(sys::Connectivity::validateCredentials(self->selectedSsid,self->selectedPassword)!=ESP_OK) {
        if(self->selectedSsid.empty() || self->selectedSsid.size()>32)
            self->notice("网络名称需1–32字节");
        else if(self->selectedPassword.size()==64)
            self->notice("64位密码须为十六进制");
        else self->notice("密码需8–63位；开放网络留空");
        return;
    }
    self->report(sys::SystemService::connectWifi(self->selectedSsid,self->selectedPassword),"先检查网络是否在附近，再连接");
}
void Settings::forgetWifiCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    if(self->v.ssid) self->selectedSsid=lv_textarea_get_text(self->v.ssid);
    self->ask(Dialog::ForgetWifi,"删除此网络保存的密码？\n当前连接也会断开。");
}
void Settings::refreshWifi(bool force) {
    auto state=sys::Connectivity::snapshot();
    const bool busy=sys::SystemService::busy(sys::ServiceDomain::Wifi) || state.transitioning;
    if(v.toggle) { if(!busy) c::checked(v.toggle,state.enabled); c::disabled(v.toggle,busy); }
    std::string status;
    if(state.transitioning) status=state.enabled?"Wi-Fi 正在切换…":"Wi-Fi 正在关闭…";
    else if(!state.enabled && state.state!=sys::WifiState::Failed) status="Wi-Fi 已关闭";
    else if(state.state==sys::WifiState::Connected) status="已连接 "+state.ssid+"\nIP "+state.ip;
    else if(state.state==sys::WifiState::Searching) status="正在查找 "+state.ssid+"…";
    else if(state.state==sys::WifiState::Connecting) status=(state.phase==sys::WifiConnectPhase::Address?"正在获取 IP ":"正在连接 ")+state.ssid+"…";
    else if(state.state==sys::WifiState::Failed) {
        status=wifiError(state.error,state.phase);
        status+="\n代码 "+std::to_string(state.error);
        if(state.retrying) status+=" · 重试 "+std::to_string(state.retry)+"/3";
    } else status="未连接，选择附近或已保存网络";
    if(page!=Page::Wifi && state.scan==sys::WifiScanState::Scanning) status+="\n正在扫描…";
    if(page==Page::Wifi && state.scan==sys::WifiScanState::Failed)
        status=state.scanPending?"扫描结束超时\n请关闭再打开 Wi-Fi":"扫描失败，请点击重试\n代码 "+std::to_string(state.scanError);
    c::setText(v.status,status.c_str());
    if(page==Page::Wifi) {
        c::disabled(v.scanButton,busy || !state.enabled || state.state==sys::WifiState::Searching || state.state==sys::WifiState::Connecting || state.scanPending);
        c::setText(lv_obj_get_child(v.scanButton,0),state.scan==sys::WifiScanState::Scanning?"扫描中":state.scanPending?"等待":state.scan==sys::WifiScanState::Failed?"重试":"扫描");
        if((force || scanRevision!=state.scanRevision) && !listPressed) refreshWifiList();
    } else if(page==Page::WifiNetwork) {
        const std::string ssid=v.ssid?lv_textarea_get_text(v.ssid):selectedSsid;
        const bool connected=state.state==sys::WifiState::Connected && state.ssid==ssid;
        c::setText(lv_obj_get_child(v.connect,0),connected?"断开连接":state.state==sys::WifiState::Connecting?"连接中…":"连接");
        c::disabled(v.connect,!state.enabled || busy || state.state==sys::WifiState::Searching || state.state==sys::WifiState::Connecting);
        c::disabled(v.forget,busy || !sys::Settings::wifiKnown(ssid));
    } else if(page==Page::WifiSaved && !busy && !listPressed) {
        // A queued Forget may finish after navigation has already returned here.
        const auto history=sys::Settings::wifiHistory();
        bool changed=history.size()!=networkCount;
        if(!changed) for(size_t i=0;i<history.size();++i) if(history[i].ssid!=networkNames[i]) changed=true;
        if(changed) { lv_obj_clean(v.body); buildSavedWifi(); }
    }
}
} // namespace apps
