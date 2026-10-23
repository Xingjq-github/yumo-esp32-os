#include "settings.hpp"
#include "core/usb_storage.hpp"
#include "ui/text_input.hpp"
#include "ui/compact_ui.hpp"
#include "theme.hpp"
#include "core/event_bus.hpp"
#include "core/settings_store.hpp"
#include "core/power_mgr.hpp"
#include "core/display_cast.hpp"
#include "core/battery_mgr.hpp"
#include "ui/gesture.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
namespace apps {
namespace c=ui::compact;
void Settings::onCreate(lv_obj_t *) {
    for (unsigned i=0;i<resultRevisions.size();++i)
        resultRevisions[i]=sys::SystemService::result(static_cast<sys::ServiceDomain>(i)).revision;
    buildPage(); timer=lv_timer_create(timerCb,50,this);
}
void Settings::onShow() { if(timer) lv_timer_resume(timer); lastRefresh=0; }
void Settings::onHide() {
    if(timer) lv_timer_pause(timer);
    if(sdRead) sdRead->cancelled.store(true);
    sdRead.reset();
    if(aboutStorageRead) aboutStorageRead->cancelled.store(true);
    aboutStorageRead.reset();
}
void Settings::onDestroy() {
    onHide();
    focusMenu.reset();
    sys::SystemService::cancelBluetoothScan();
    sys::SystemService::cancelWifiScan();
    if(timer) { lv_timer_delete(timer); timer=nullptr; }
    // The window manager owns the root and all its widgets. No async callback
    // retains `this`; queued service commands own only copied values.
    v={}; modal=toast=nullptr;
}
void Settings::navigate(Page target) { nextPage=target; pagePending=true; }
bool Settings::onBack() {
    if(modal || pendingDialog!=Dialog::None) { dialogAnswer=-1; return true; }
    switch(page) {
    case Page::Root: return false;
    case Page::WifiNetwork: case Page::WifiSaved: navigate(Page::Wifi); break;
    case Page::BluetoothScan: case Page::BluetoothDevice: navigate(Page::Bluetooth); break;
    default: navigate(Page::Root); break;
    }
    return true;
}
void Settings::backCb(lv_event_t *e) { (void)e; sys::EventBus::publish({sys::EventType::BackRequested}); }
void Settings::pageCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    auto page=static_cast<Page>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
    self->navigate(page);
}
lv_obj_t *Settings::menuRow(const char *title,Page target,lv_obj_t **detail) {
    char numbered[96];
    if(ui::theme::terminal() && page==Page::Root) {
        std::snprintf(numbered,sizeof(numbered),"%02u %s",static_cast<unsigned>(lv_obj_get_child_count(v.body)+1),title);
        title=numbered;
    }
    auto row=c::row(v.body,title,detail?"":nullptr,pageCb,this,detail);
    lv_obj_set_user_data(row,reinterpret_cast<void *>(static_cast<intptr_t>(target))); return row;
}
void Settings::buildPage() {
    if(sdRead) sdRead->cancelled.store(true);
    sdRead.reset();
    if(aboutStorageRead) aboutStorageRead->cancelled.store(true);
    aboutStorageRead.reset();aboutUsage={};aboutSdEpoch=0;
    ui::TextInput::hide();
    focusMenu.reset();
    if(v.page) lv_obj_delete(v.page);
    v={}; listPressed=false; scanRevision=bondRevision=UINT32_MAX; scanShown=0;
    v.page=c::panel(root(),c::kWidth,c::kHeight);
    // Root is a full-screen list; subpages retain their title and back target.
    const int bodyTop=page==Page::Root?0:c::kHeader+1;
    if(page!=Page::Root) {
        auto header=c::panel(v.page,c::kWidth,c::kHeader);
        auto back=c::button(header,LV_SYMBOL_LEFT,backCb,this,40,c::kHeader); lv_obj_set_pos(back,0,0);
        const char *titles[]={"设置","Wi-Fi","已保存网络","网络详情","蓝牙","已配对设备","附近 BLE 设备","显示与声音","系统与电源","日期与时间","关于设备","主题"};
        auto title=c::label(header,titles[static_cast<int>(page)],196); lv_obj_align(title,LV_ALIGN_LEFT_MID,42,0);
    }
    v.body=c::panel(v.page,c::kWidth,c::kHeight-bodyTop,true);
    lv_obj_set_pos(v.body,0,bodyTop); lv_obj_set_flex_flow(v.body,LV_FLEX_FLOW_COLUMN);
    switch(page) {
    case Page::Root: buildRoot(); break;
    case Page::Wifi: buildWifi(); break;
    case Page::WifiSaved: buildSavedWifi(); break;
    case Page::WifiNetwork: buildNetwork(); break;
    case Page::Bluetooth: buildBluetooth(); break;
    case Page::BluetoothDevice: buildBluetoothDevice(); break;
    case Page::BluetoothScan: buildBluetoothScan(); break;
    case Page::Display: buildDisplay(); break;
    case Page::System: buildSystem(); break;
    case Page::Time: buildTime(); break;
    case Page::About: buildAbout(); break;
    case Page::Theme: buildTheme(); break;
    }
    refresh(true);
    if(ui::theme::focus() && page!=Page::Root)ui::Gesture::hook(v.page);
}
void Settings::buildRoot() {
    if(ui::theme::focus()) {
        focusMenu.create(v.body,240,7,focusIndex,this,
            [](void *owner,size_t i){auto self=static_cast<Settings *>(owner);self->focusIndex=i;self->refreshFocusRoot();},
            [](void *owner,size_t i){
                constexpr Page pages[]={Page::Wifi,Page::Bluetooth,Page::Display,Page::Theme,Page::System,Page::Time,Page::About};
                static_cast<Settings *>(owner)->navigate(pages[i]);
            });
        return;
    }
    menuRow("Wi-Fi",Page::Wifi,&v.wifiSummary);
    auto btRow=menuRow("蓝牙",Page::Bluetooth,&v.btSummary);
    lv_obj_set_width(lv_obj_get_child(btRow,0),64);lv_obj_set_width(v.btSummary,152);lv_obj_align(v.btSummary,LV_ALIGN_TOP_RIGHT,-17,0);
    lv_obj_set_height(v.btSummary,ui::theme::fontSmall()->line_height);
    lv_label_set_long_mode(v.btSummary,LV_LABEL_LONG_MODE_DOTS);
    v.btAddress=c::label(btRow,"",152);lv_obj_set_style_text_font(v.btAddress,&lv_font_montserrat_10,0);
    lv_obj_set_style_text_color(v.btAddress,ui::theme::muted(),0);lv_obj_set_style_text_align(v.btAddress,LV_TEXT_ALIGN_RIGHT,0);lv_obj_align(v.btAddress,LV_ALIGN_BOTTOM_RIGHT,-17,-1);
    menuRow("显示与声音",Page::Display);
    menuRow("主题",Page::Theme);
    menuRow("系统与电源",Page::System);
    menuRow("日期与时间",Page::Time);
    menuRow("关于设备",Page::About);
    if(ui::theme::tiles()) {
        // Two network rows retain their summaries/addresses. Other controls
        // are compact 2-column tiles, not a second copy of their business logic.
        lv_obj_set_flex_flow(v.body,LV_FLEX_FLOW_ROW_WRAP);
        for(uint32_t i=2;i<lv_obj_get_child_count(v.body);++i) {
            auto tile=lv_obj_get_child(v.body,static_cast<int32_t>(i));
            lv_obj_set_size(tile,119,52);
            auto name=lv_obj_get_child(tile,0);
            lv_obj_set_width(name,100);
            lv_obj_align(name,LV_ALIGN_LEFT_MID,4,0);
            lv_obj_set_hidden(lv_obj_get_child(tile,-1),true);
        }
    }
}
void Settings::refreshFocusRoot() {
    const char *names[]={"Wi-Fi","蓝牙","显示与声音","主题","系统与电源","日期与时间","关于设备"};
    const char *icons[]={LV_SYMBOL_WIFI,LV_SYMBOL_BLUETOOTH,LV_SYMBOL_VOLUME_MID,LV_SYMBOL_IMAGE,LV_SYMBOL_POWER,LV_SYMBOL_BELL,LV_SYMBOL_SETTINGS};
    const char *detail="点击打开设置";char address[18]{};
    const auto wifi=sys::Connectivity::snapshot();const auto bt=sys::SystemService::bluetooth();
    if(focusIndex==0)detail=wifi.transitioning?"切换中":!wifi.enabled?"已关闭":wifi.state==sys::WifiState::Connected?wifi.ssid.c_str():"未连接";
    if(focusIndex==1) {
        const bool peer=bt.peer.connected || bt.state==sys::BleHidState::Connecting;
        detail=peer?(bt.peer.name[0]?bt.peer.name:"未提供名称"):bt.hasError()?"蓝牙异常":!bt.enabled?"已关闭":"未连接";
        if(peer)sys::BleHid::formatAddr(bt.peer,address,sizeof(address));
    }
    if(focusIndex==3)detail=sys::themes::name(ui::theme::current());
    focusMenu.content(names[focusIndex],icons[focusIndex],detail,address);
}
void Settings::buildTheme() {
    constexpr ui::theme::Id ids[]={ui::theme::Id::Warm,ui::theme::Id::Mono,ui::theme::Id::Tiles,ui::theme::Id::Focus,ui::theme::Id::Terminal};
    const char *details[]={"暖色圆角 / 双列应用","浅色线框 / 双列应用","信息磁贴 / 分区布局","左右滑动 / 单卡片","编号列表 / 游标确认"};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);++i) {
        auto row=c::button(v.body,"",[](lv_event_t *e) {
            auto self=static_cast<Settings *>(lv_event_get_user_data(e));
            if(self->themeSubmitted) return;
            const auto id=static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
            if(id==static_cast<uint8_t>(ui::theme::current())) {self->notice("已在使用此主题");return;}
            self->selectedTheme=id;
            self->ask(Dialog::Theme,"保存主题并重启？\n退出应用并断开连接；重启后尝试连接最近设备。");
        },this,240,54);
        lv_obj_set_user_data(row,reinterpret_cast<void *>(static_cast<uintptr_t>(ids[i])));
        auto name=c::label(row,sys::themes::name(ids[i]),168);lv_obj_set_pos(name,5,1);
        auto detail=c::label(row,details[i],220);lv_obj_set_pos(detail,5,25);
        if(ids[i]==ui::theme::current()) {
            auto check=c::label(row,LV_SYMBOL_OK);lv_obj_align(check,LV_ALIGN_TOP_RIGHT,-7,4);
            lv_obj_set_style_border_width(row,2,0);lv_obj_set_style_border_color(row,ui::theme::accent(),0);
        }
    }
    c::section(v.body,"选择后保存并重启");
}
void Settings::markListTouch(lv_obj_t *row) {
    lv_obj_add_event_cb(row,[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        auto code=lv_event_get_code(e);
        if(code==LV_EVENT_PRESSED) self->listPressed=true;
        if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) self->listPressed=false;
    },LV_EVENT_ALL,this);
}
void Settings::request(sys::ServiceAction action,bool value) { report(sys::SystemService::request(action,value)); }
void Settings::report(esp_err_t err,const char *success) {
    if(err==ESP_OK) { if(success) notice(success); return; }
    if(const auto message=sys::SystemService::errorText(err)) notice(message);
    else { char text[100]; std::snprintf(text,sizeof(text),"操作失败: %s",esp_err_to_name(err)); notice(text); }
}
void Settings::notice(const char *message) { noticeText=message; noticePending=true; }
void Settings::ask(Dialog kind,const char *message) { pendingDialog=kind; dialogText=message; }
void Settings::showDialog() {
    ui::TextInput::hide();
    closeDialog(); dialog=pendingDialog; pendingDialog=Dialog::None;
    modal=c::panel(root(),c::kWidth,c::kHeight);
    lv_obj_set_user_flag(modal,0,true);
    auto title=c::label(modal,"请确认",236); lv_obj_set_pos(title,1,12);
    auto message=c::label(modal,dialogText.c_str(),236); lv_label_set_long_mode(message,LV_LABEL_LONG_MODE_WRAP);
    if(dialog==Dialog::ForgetBond) {
        ui::BluetoothIdentity identity;identity.create(modal,236,1,40);identity.update(selectedBond);
        lv_obj_set_size(message,236,100);lv_obj_set_pos(message,1,84);
    } else { lv_obj_set_size(message,236,128);lv_obj_set_pos(message,1,48); }
    auto no=c::button(modal,"取消",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->dialogAnswer=-1;},this,119,40);
    auto yes=c::button(modal,"确定",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->dialogAnswer=1;},this,120,40);
    lv_obj_set_pos(no,0,199); lv_obj_set_pos(yes,120,199);
    c::buttonBackground(yes,dialog==Dialog::Theme?ui::theme::accent():ui::theme::danger());
}
void Settings::closeDialog() { if(modal) lv_obj_delete(modal); modal=nullptr; dialog=Dialog::None; }
void Settings::processDialog() {
    if(!dialogAnswer) return;
    const bool yes=dialogAnswer>0; dialogAnswer=0;
    if(pendingDialog!=Dialog::None) {
        pendingDialog=Dialog::None;
        return;
    }
    esp_err_t err=ESP_OK;
    if(yes) {
        switch(dialog) {
        case Dialog::ForgetWifi: err=sys::SystemService::forgetWifi(selectedSsid); if(err==ESP_OK) navigate(Page::WifiSaved); break;
        case Dialog::ForgetBond: err=sys::SystemService::forgetBond(selectedBond); if(err==ESP_OK) navigate(Page::Bluetooth); break;
        case Dialog::ForgetAll: err=sys::SystemService::request(sys::ServiceAction::BluetoothForgetAll); break;
        case Dialog::DeepSleep: err=sys::SystemService::request(sys::ServiceAction::DeepSleep); break;
        case Dialog::PowerOff: err=sys::SystemService::request(sys::ServiceAction::PowerOff); break;
        case Dialog::UsbFlash: err=sys::UsbStorage::setEnabled(false); break;
        case Dialog::Theme:
            err=sys::SystemService::switchTheme(selectedTheme);
            themeSubmitted=err==ESP_OK;
            if(themeSubmitted) notice("正在结束应用并保存主题");
            break;
        default: break;
        }
    }
    closeDialog(); report(err);
}
void Settings::refresh(bool force) {
    const auto wifi=sys::Connectivity::snapshot();
    const auto bt=sys::SystemService::bluetooth();
    if(page==Page::Root) {
        if(ui::theme::focus())refreshFocusRoot();
        const char *w=wifi.state==sys::WifiState::Failed?"连接失败":wifi.transitioning?"切换中":!wifi.enabled?"已关闭":wifi.state==sys::WifiState::Connected?wifi.ssid.c_str():wifi.state==sys::WifiState::Searching?"查找中":wifi.state==sys::WifiState::Connecting?"连接中":"未连接";
        c::setText(v.wifiSummary,w);
        const bool peerShown=bt.peer.connected || bt.state==sys::BleHidState::Connecting;
        c::setText(v.btSummary,peerShown?(bt.peer.name[0]?bt.peer.name:"未提供名称"):bt.hasError()?"蓝牙异常":!bt.enabled?"已关闭":bt.state==sys::BleHidState::Starting?"启动中":"未连接");
        char address[18]{};if(peerShown) sys::BleHid::formatAddr(bt.peer,address,sizeof(address));c::setText(v.btAddress,address);
    }
    if(page==Page::Wifi || page==Page::WifiNetwork || page==Page::WifiSaved) refreshWifi(force);
    if(page==Page::Bluetooth || page==Page::BluetoothScan || page==Page::BluetoothDevice) refreshBluetooth(force);
    if(page==Page::Display) {
        refreshSupply();
        c::checked(v.castToggle,sys::DisplayCast::enabled());
        c::checked(v.castScreenOff,sys::Settings::castScreenOff());
        c::setText(v.castStatus,sys::DisplayCast::statusText());
        if(!lv_obj_has_state(v.volume,LV_STATE_PRESSED) && lv_slider_get_value(v.volume)!=sys::Settings::volume()) lv_slider_set_value(v.volume,sys::Settings::volume(),LV_ANIM_OFF);
        char text[16]; std::snprintf(text,sizeof(text),"%d%%",sys::Settings::volume()); c::setText(v.volumeValue,text);
        std::snprintf(text,sizeof(text),"%d%%",sys::Settings::brightness()); c::setText(v.brightnessValue,text);
    }
    if(page==Page::System) {
        lv_dropdown_set_selected(v.usbMode,sys::UsbStorage::enabled()?1:0);
        c::disabled(v.usbMode,sys::UsbStorage::transitioning());
        c::setText(v.usbStatus,sys::UsbStorage::statusText());
        refreshSupply();
        refreshSd();
        const auto battery=sys::Battery::snapshot();
        const char *charge=!battery.chargingKnown?"充电状态未知":battery.charging?"充电中":"未在充电";
        char text[112];
        if(battery.valid) std::snprintf(text,sizeof(text),"电池估算：%d%%  %d.%03d V\n%s",battery.percent,
            battery.millivolts/1000,battery.millivolts%1000,charge);
        else std::snprintf(text,sizeof(text),"电池估算：--%%\n电压读取不可用 / %s",charge);
        c::setText(v.battery,text);
        if(!sys::SystemService::busy(sys::ServiceDomain::System)) c::checked(v.motion,sys::Settings::motionSensorOn());
        c::disabled(v.motion,sys::SystemService::busy(sys::ServiceDomain::System));
    }
    if(page==Page::Time) refreshTime(force);
    if(page==Page::About && (force || lv_tick_get()-aboutRefreshed>=1000)) {
        aboutRefreshed=lv_tick_get();refreshAbout();
    }
    for(unsigned i=0;i<resultRevisions.size();++i) {
        auto result=sys::SystemService::result(static_cast<sys::ServiceDomain>(i));
        if(result.revision!=resultRevisions[i]) { resultRevisions[i]=result.revision; if(result.error!=ESP_OK) { if(result.action==sys::ServiceAction::ThemeRestart) themeSubmitted=false; report(result.error); } }
    }
}
void Settings::tick() {
    processDialog();
    if(pagePending) {
        if(page==Page::BluetoothScan) sys::SystemService::cancelBluetoothScan();
        page=nextPage; pagePending=false; buildPage();
    }
    if(pendingDialog!=Dialog::None) showDialog();
    if(noticePending) {
        if(toast) lv_obj_delete(toast);
        toast=c::panel(root(),c::kWidth,52); lv_obj_set_style_bg_color(toast,ui::theme::surface(),0);
        lv_obj_align(toast,LV_ALIGN_BOTTOM_MID,0,0);
        auto text=c::label(toast,noticeText.c_str(),238); lv_label_set_long_mode(text,LV_LABEL_LONG_MODE_WRAP); lv_obj_center(text);
        toastUntil=lv_tick_get()+2500; noticePending=false;
    }
    if(toast && static_cast<int32_t>(lv_tick_get()-toastUntil)>=0) { lv_obj_delete(toast); toast=nullptr; }
    if(sys::Power::isScreenOn() && lv_tick_get()-lastRefresh>=250) { lastRefresh=lv_tick_get(); refresh(); }
}
void Settings::timerCb(lv_timer_t *timer) { static_cast<Settings *>(lv_timer_get_user_data(timer))->tick(); }
} // namespace apps
