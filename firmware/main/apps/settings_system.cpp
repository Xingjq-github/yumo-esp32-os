#include "settings.hpp"
#include "core/usb_storage.hpp"
#include "ui/compact_ui.hpp"
#include "ui/sysinfo_overlay.hpp"
#include "theme.hpp"
#include "ui/sleep_prompt.hpp"
#include "core/settings_store.hpp"
#include "core/power_mgr.hpp"
#include "core/display_cast.hpp"
#include "core/event_bus.hpp"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "core/battery_mgr.hpp"
#include "core/qmi8658.hpp"
#include "ui/window_mgr.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <new>
#include <sys/time.h>
namespace apps {
namespace c=ui::compact;
namespace {
constexpr uint16_t PowerTimes[]={0,30,60,120,180,240,300,600,900,1800};
const char *PowerTimeOptions="关闭\n30秒\n1分钟\n2分钟\n3分钟\n4分钟\n5分钟\n10分钟\n15分钟\n30分钟";
}
lv_obj_t *Settings::powerTime(const char *title,uint16_t seconds) {
    auto row=c::panel(v.body,240,40);
    auto label=c::label(row,title,135);lv_obj_align(label,LV_ALIGN_LEFT_MID,0,0);
    auto input=c::dropdown(row,PowerTimeOptions,103);lv_obj_align(input,LV_ALIGN_RIGHT_MID,0,0);
    for(unsigned i=0;i<std::size(PowerTimes);++i)if(PowerTimes[i]==seconds)lv_dropdown_set_selected(input,i);
    lv_obj_add_event_cb(input,powerOptionCb,LV_EVENT_VALUE_CHANGED,this);return input;
}
void Settings::powerOptionCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));auto target=lv_event_get_target_obj(e);
    auto cfg=sys::Settings::powerConfig();
    if(target==self->v.autoSleep)cfg.automatic=lv_obj_has_state(target,LV_STATE_CHECKED);
    else if(target==self->v.protectLinks)cfg.protectLinks=lv_obj_has_state(target,LV_STATE_CHECKED);
    else if(target==self->v.sleepOnPower)cfg.sleepOnPower=lv_obj_has_state(target,LV_STATE_CHECKED);
    else if(target==self->v.screenOffOnPower)cfg.screenOffOnPower=lv_obj_has_state(target,LV_STATE_CHECKED);
    else if(target==self->v.powerStrategy)cfg.longest=lv_dropdown_get_selected(target)==0;
    else {
        const auto value=PowerTimes[std::min<size_t>(lv_dropdown_get_selected(target),std::size(PowerTimes)-1)];
        if(target==self->v.screenTime)cfg.screenSeconds=value;
        if(target==self->v.quietTime)cfg.quietSeconds=value;
        if(target==self->v.standbyTime)cfg.standbySeconds=value;
        if(target==self->v.deepTime)cfg.deepSeconds=value;
        if(cfg.deepSeconds && cfg.standbySeconds && cfg.deepSeconds<=cfg.standbySeconds) {
            // Keep both visible choices valid; reject instead of silently using
            // an unlisted timeout. Both values are measured from screen-off.
            const auto old=sys::Settings::powerConfig();const auto restore=target==self->v.deepTime?old.deepSeconds:old.standbySeconds;
            for(unsigned i=0;i<std::size(PowerTimes);++i)if(PowerTimes[i]==restore)lv_dropdown_set_selected(target,i);
            self->notice("深睡时间需要晚于省电待机");return;
        }
    }
    sys::Settings::setPowerConfig(cfg);sys::Power::activity();
}
void Settings::buildDisplay() {
    auto slider=[&](const char *title,int value,int minimum,int maximum,lv_obj_t **number) {
        auto row=c::panel(v.body,240,60);
        auto label=c::label(row,title,172); lv_obj_set_pos(label,1,1);
        *number=c::label(row,"",62); lv_obj_set_style_text_align(*number,LV_TEXT_ALIGN_RIGHT,0); lv_obj_set_pos(*number,176,1);
        auto slider=lv_slider_create(row); lv_obj_set_size(slider,216,14); lv_obj_set_pos(slider,11,34);
        lv_slider_set_range(slider,minimum,maximum); lv_slider_set_value(slider,value,LV_ANIM_OFF);
        lv_obj_set_style_pad_all(slider,1,0); lv_obj_set_style_pad_all(slider,4,LV_PART_KNOB);
        lv_obj_set_ext_click_area(slider,9); // 32px touch height, within its 60px row
        lv_obj_set_gesture_bubble(slider,false);
        lv_obj_add_event_cb(slider,sliderCb,LV_EVENT_VALUE_CHANGED,this);
        return slider;
    };
    v.brightness=slider("屏幕亮度",sys::Settings::brightness(),5,100,&v.brightnessValue);
    v.volume=slider("扬声器音量",sys::Settings::volume(),0,100,&v.volumeValue);
    const auto cfg=sys::Settings::powerConfig();
    v.quietBrightness=slider("省电亮屏亮度",cfg.brightness,5,100,&v.quietValue);
    char percent[12];std::snprintf(percent,sizeof(percent),"%u%%",unsigned(cfg.brightness));c::setText(v.quietValue,percent);
    v.castToggle=c::toggle(v.body,"USB 画面投放",sys::DisplayCast::enabled(),[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        self->report(sys::DisplayCast::setEnabled(lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED)));
        c::checked(self->v.castToggle,sys::DisplayCast::enabled());
    },this);
    v.castScreenOff=c::toggle(v.body,"USB投屏时熄屏",sys::Settings::castScreenOff(),[](lv_event_t *e) {
        const bool on=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
        sys::Settings::setCastScreenOff(on);
        sys::DisplayCast::poll(); // GUI-owned; active casting updates immediately.
    },this);
    v.screenOffOnPower=c::toggle(v.body,"接通电源时熄屏",cfg.screenOffOnPower,powerOptionCb,this);
    v.screenTime=powerTime("自动黑屏",cfg.screenSeconds);
    v.quietTime=powerTime("常亮省电等待",cfg.quietSeconds);
    buildSupplyButton();
    buildSupplyHelp();
    v.castStatus=c::label(v.body,sys::DisplayCast::statusText(),238);
    lv_label_set_long_mode(v.castStatus,LV_LABEL_LONG_MODE_WRAP);
    auto powerHelp=c::label(v.body,"阅读、时钟、手电常亮省电；手电和SOS保持亮度。",238);
    lv_label_set_long_mode(powerHelp,LV_LABEL_LONG_MODE_WRAP);
    auto help=c::label(v.body,"拖动即生效，设置自动保存。",238);
    lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
    auto castHelp=c::label(v.body,"电脑打开接收端并连接。默认双屏；开启熄屏选项后，仅关本机背光。",238);
    lv_label_set_long_mode(castHelp,LV_LABEL_LONG_MODE_WRAP);
    auto castControl=c::label(v.body,"投屏时触控、按键和声音照常；断开恢复显示，关屏结束投屏。",238);
    lv_label_set_long_mode(castControl,LV_LABEL_LONG_MODE_WRAP);
}
void Settings::sliderCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e)); auto slider=lv_event_get_target_obj(e);
    const int value=lv_slider_get_value(slider);
    if(slider==self->v.brightness) { sys::Settings::setBrightness(value); sys::Power::applyBrightness(value); }
    else if(slider==self->v.quietBrightness) {auto cfg=sys::Settings::powerConfig();cfg.brightness=value;sys::Settings::setPowerConfig(cfg);}
    else sys::SystemService::setVolume(value);
    char text[16]; std::snprintf(text,sizeof(text),"%d%%",value);
    c::setText(slider==self->v.brightness?self->v.brightnessValue:slider==self->v.quietBrightness?self->v.quietValue:self->v.volumeValue,text);
}
void Settings::buildSystem() {
    const auto cfg=sys::Settings::powerConfig();
    v.autoSleep=c::toggle(v.body,"自动休眠",cfg.automatic,powerOptionCb,this);
    v.sleepOnPower=c::toggle(v.body,"接通电源时休眠",cfg.sleepOnPower,powerOptionCb,this);
    v.protectLinks=c::toggle(v.body,"连接时禁止深睡",cfg.protectLinks,powerOptionCb,this);
    v.sysinfo=c::toggle(v.body,"系统信息浮层",sys::Settings::sysInfoOn(),systemToggleCb,this);
    v.motion=c::toggle(v.body,"运动传感器",sys::Settings::motionSensorOn(),systemToggleCb,this);
    auto usbRow=c::panel(v.body,240,40);
    auto usbTitle=c::label(usbRow,"USB连接模式",108);lv_obj_align(usbTitle,LV_ALIGN_LEFT_MID,0,0);
    v.usbMode=c::dropdown(usbRow,"开发与烧录\nSD卡读卡器",130);lv_obj_align(v.usbMode,LV_ALIGN_RIGHT_MID,0,0);
    lv_dropdown_set_selected(v.usbMode,sys::UsbStorage::enabled()?1:0);
    lv_obj_add_event_cb(v.usbMode,[](lv_event_t *e){
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        const bool on=lv_dropdown_get_selected(self->v.usbMode)==1;
        if(!on&&sys::UsbStorage::enabled())
            self->ask(Dialog::UsbFlash,"切回开发与烧录？\n先停止复制；强制切换后，未完成的传输可能丢失。");
        else if(sys::UsbStorage::setEnabled(on)!=ESP_OK)
            self->notice("请关闭投屏，等待SD读写结束");
        lv_dropdown_set_selected(self->v.usbMode,sys::UsbStorage::enabled()?1:0);
    },LV_EVENT_VALUE_CHANGED,this);
    auto policyRow=c::panel(v.body,240,40);
    auto policyTitle=c::label(policyRow,"常亮省电策略",130);lv_obj_align(policyTitle,LV_ALIGN_LEFT_MID,0,0);
    v.powerStrategy=c::dropdown(policyRow,"最长续航\n保持连接",108);lv_obj_align(v.powerStrategy,LV_ALIGN_RIGHT_MID,0,0);
    lv_dropdown_set_selected(v.powerStrategy,cfg.longest?0:1);lv_obj_add_event_cb(v.powerStrategy,powerOptionCb,LV_EVENT_VALUE_CHANGED,this);
    v.standbyTime=powerTime("熄屏后省电待机",cfg.standbySeconds);
    v.deepTime=powerTime("熄屏后深度睡眠",cfg.deepSeconds);
    v.sdEject=c::button(v.body,"安全弹出 SD 卡",[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        if(core::sd::busy() || self->sdRead) return;
        if(core::sd::state()==core::sd::State::Ejected) {
            // Explicit read only: the unconnected CD pin cannot detect insertion.
            try { self->sdRead=core::listDirectory(core::sd::Root,false); }
            catch(const std::bad_alloc &) { self->notice("内存不足，请稍后重试"); }
        } else if(!core::sd::requestEject()) self->notice("SD操作忙，请稍后重试");
        self->refreshSd();
    },this);
    c::button(v.body,"深度睡眠",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->ask(Dialog::DeepSleep,ui::DeepSleepPrompt);},this);
    c::button(v.body,"关机",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->ask(Dialog::PowerOff,"保存并关机？\nUSB供电时改为深睡。");},this);
    buildSupplyButton();
    buildSupplyHelp();
    v.usbStatus=c::label(v.body,sys::UsbStorage::statusText(),238);lv_label_set_long_mode(v.usbStatus,LV_LABEL_LONG_MODE_WRAP);
    auto usbHelp=c::label(v.body,"读卡器由电脑独占SD。自动深睡会断开，请先在电脑弹出。开机恢复开发模式。",238);lv_label_set_long_mode(usbHelp,LV_LABEL_LONG_MODE_WRAP);
    v.sdHelp=c::label(v.body,"",238);lv_label_set_long_mode(v.sdHelp,LV_LABEL_LONG_MODE_WRAP);
    auto autoHelp=c::label(v.body,"熄屏后逐级省电；插电设置优先，连接保护仅阻止深睡。保护解除后重新计时。",238);
    lv_label_set_long_mode(autoHelp,LV_LABEL_LONG_MODE_WRAP);
    v.battery=c::label(v.body,"",238); lv_label_set_long_mode(v.battery,LV_LABEL_LONG_MODE_WRAP);
    auto help=c::label(v.body,"PWR 单击亮灭屏，长按关机；深睡后快速双击唤醒。",238); lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
}
void Settings::buildSupplyButton() {
    v.confirmBattery=c::button(v.body,"已拔电，恢复省电",[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        if(!sys::Battery::supplyProtected())sys::Battery::clearSupplyConfirmation();
        else sys::Battery::confirmBatterySupply();
        sys::Power::activity();
        self->notice("正在确认，请查看下方供电状态");
    },this);
}
void Settings::buildSupplyHelp() {
    v.supplyStatus=c::label(v.body,"",238);lv_label_set_long_mode(v.supplyStatus,LV_LABEL_LONG_MODE_WRAP);
    auto help=c::label(v.body,"插电开关分别控制自动熄屏和休眠，手动操作不受限。",238);
    lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
    auto supplyHelp=c::label(v.body,"30秒无外电信号后按电池省电。满电充电器可能无法识别，可手动恢复插电保护。",238);
    lv_label_set_long_mode(supplyHelp,LV_LABEL_LONG_MODE_WRAP);refreshSupply();
}
void Settings::refreshSupply() {
    const auto state=sys::Battery::supplyState();
    const char *text=state==sys::supply::State::External?"供电：USB在线或充电中":
        state==sys::supply::State::BatteryConfirmed?"供电：已确认使用电池":
        state==sys::supply::State::BatteryInferred?"供电：无外电信号，按电池省电":
        state==sys::supply::State::Protected?"供电：手动插电保护":"供电待判定：暂按插电保护";
    c::setText(v.supplyStatus,text);
    c::setText(lv_obj_get_child(v.confirmBattery,0),!sys::supply::protects(state)?"恢复插电保护":"已拔电，恢复省电");
    c::disabled(v.confirmBattery,sys::supply::protects(state) && !sys::Battery::canConfirmBatterySupply());
}
void Settings::refreshSd() {
    if(sdRead && sdRead->done.load(std::memory_order_acquire)) {
        if(sdRead->error) notice("SD读取失败，请检查插卡后重试");
        sdRead.reset();
    }
    const auto state=core::sd::state();
    const char *title="安全弹出 SD 卡";
    const char *help="弹出完成后再拔卡。卡槽仍供电。";
    switch(state) {
    case core::sd::State::Usb: title="电脑正在使用 SD 卡";help="先停止电脑复制，再切回开发模式。";break;
    case core::sd::State::Ejecting: title="正在安全弹出…";help="正在保存并停止读写，请勿拔卡。";break;
    case core::sd::State::Ejected: title="可插拔SD卡";help="已停止读写，卡槽仍供电。\n插卡后点此读取，或到应用刷新。";break;
    case core::sd::State::Reading: title="正在读取 SD 卡…";help="正在读取，请勿拔卡。";break;
    case core::sd::State::Failed: help=core::sd::ejectError();break;
    case core::sd::State::Available: break;
    }
    if(sdRead) {title="正在读取 SD 卡…";help="正在读取，请勿拔卡。";}
    c::setText(lv_obj_get_child(v.sdEject,0),title);
    c::setText(v.sdHelp,help);
    c::disabled(v.sdEject,core::sd::busy() || bool(sdRead));
}
void Settings::systemToggleCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e)); auto target=lv_event_get_target_obj(e);
    const bool on=lv_obj_has_state(target,LV_STATE_CHECKED);
    if(target==self->v.sysinfo) { sys::Settings::setSysInfoOn(on); ui::SysInfoOverlay::setVisible(on); }
    else self->request(sys::ServiceAction::Motion,on);
}
void Settings::buildTime() {
    const auto now=time(nullptr); tm local{}; localtime_r(&now,&local);
    const int values[]={std::max(2020,std::min(2099,local.tm_year+1900)),local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec};
    const int first[]={2020,1,1,0,0,0},last[]={2099,12,31,23,59,59};
    for(int group=0;group<2;++group) {
        c::section(v.body,group==0?"年 / 月 / 日":"时 / 分 / 秒");
        auto row=c::panel(v.body,240,36); lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
        for(int column=0;column<3;++column) {
            int index=group*3+column; std::string options;
            for(int value=first[index];value<=last[index];++value) {
                char text[12]; std::snprintf(text,sizeof(text),index==0?"%04d":"%02d",value);
                if(!options.empty()) {
                    options+='\n';
                }
                options+=text;
            }
            v.date[index]=c::dropdown(row,options.c_str(),column==2?80:79);
            lv_dropdown_set_selected(v.date[index],values[index]-first[index]);
        }
    }
    c::button(v.body,"保存日期与时间",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->saveTime();},this,LV_PCT(100),36);
    v.timeSyncButton=c::button(v.body,"使用网络校时",[](lv_event_t *e){
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        if(sys::Connectivity::wifiState()!=sys::WifiState::Connected) {self->notice("请先连接 Wi-Fi");return;}
        self->report(sys::SystemService::request(sys::ServiceAction::SyncTime));
    },this,LV_PCT(100),36);
    v.timeStatus=c::label(v.body,"",236);
    lv_label_set_long_mode(v.timeStatus,LV_LABEL_LONG_MODE_WRAP);
    timeRevision=UINT32_MAX;
}
void Settings::refreshTime(bool force) {
    const auto state=sys::SystemService::timeSync();
    const bool busy=sys::SystemService::timeSyncBusy();
    c::disabled(v.timeSyncButton,busy);
    c::setText(lv_obj_get_child(v.timeSyncButton,0),busy?"正在网络校时…":"使用网络校时");
    if(!force && timeRevision==state.revision) return;
    timeRevision=state.revision;
    char text[128]{};
    switch(state.state) {
    case sys::TimeSyncState::Running:
        std::snprintf(text,sizeof(text),"正在校时 %u/3，请保持 Wi-Fi 连接",unsigned(state.server));break;
    case sys::TimeSyncState::Synced:
    case sys::TimeSyncState::Manual: {
        const auto now=time(nullptr);tm local{};localtime_r(&now,&local);
        const int values[]={std::max(2020,std::min(2099,local.tm_year+1900))-2020,local.tm_mon,local.tm_mday-1,local.tm_hour,local.tm_min,local.tm_sec};
        for(unsigned i=0;i<v.date.size();++i) {
            lv_dropdown_close(v.date[i]);lv_dropdown_set_selected(v.date[i],values[i]);
        }
        const char *message=state.state==sys::TimeSyncState::Synced?"网络校时成功":"日期与时间已应用";
        std::snprintf(text,sizeof(text),"%s\n%04d-%02d-%02d %02d:%02d:%02d",message,local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec);
        if(!force) notice(message);
        break;
    }
    case sys::TimeSyncState::Failed:
        if(state.error==ESP_ERR_TIMEOUT) std::snprintf(text,sizeof(text),"校时超时，请检查网络后重试");
        else std::snprintf(text,sizeof(text),"校时失败：%s",esp_err_to_name(state.error));
        if(!force) notice(text);
        break;
    case sys::TimeSyncState::Offline: std::snprintf(text,sizeof(text),"校时未完成，请连接 Wi-Fi 后重试");break;
    case sys::TimeSyncState::Cancelled: std::snprintf(text,sizeof(text),"校时已取消，可再次重试");break;
    default: std::snprintf(text,sizeof(text),"连接 Wi-Fi 后可使用网络校时");break;
    }
    c::setText(v.timeStatus,text);
}
void Settings::saveTime() {
    int year=2020+lv_dropdown_get_selected(v.date[0]);
    int month=1+lv_dropdown_get_selected(v.date[1]); int day=1+lv_dropdown_get_selected(v.date[2]);
    const bool leap=year%4==0 && (year%100!=0 || year%400==0);
    const int days[]={31,28+(leap?1:0),31,30,31,30,31,31,30,31,30,31};
    if(month<1 || month>12 || day<1 || day>days[month-1]) { notice("日期不存在，请检查月份和天数"); return; }
    tm value{}; value.tm_year=year-1900; value.tm_mon=month-1; value.tm_mday=day;
    value.tm_hour=lv_dropdown_get_selected(v.date[3]); value.tm_min=lv_dropdown_get_selected(v.date[4]); value.tm_sec=lv_dropdown_get_selected(v.date[5]); value.tm_isdst=-1;
    const auto epoch=mktime(&value);
    if(epoch<0) {notice("时间设置失败");return;}
    report(sys::SystemService::setClock(epoch));
}
void Settings::buildAbout() {
    // Created only on entry; dynamic values reuse widgets once per second.
    lv_obj_set_style_pad_row(v.body,4,0);
    auto card=[&](int height) {
        auto obj=c::panel(v.body,240,height);
        lv_obj_set_style_bg_color(obj,ui::theme::surface(),0);
        lv_obj_set_style_radius(obj,ui::theme::radius(),0);return obj;
    };
    auto heading=[&](const char *icon,const char *title) {
        auto row=c::panel(v.body,240,26);
        auto mark=c::label(row,icon,22);lv_obj_align(mark,LV_ALIGN_LEFT_MID,7,0);
        lv_obj_set_style_text_color(mark,ui::theme::accent(),0);
        auto text=c::label(row,title,200);lv_obj_align(text,LV_ALIGN_LEFT_MID,32,0);
    };
    auto field=[&](const char *title,const char *value,bool tall=false) {
        auto row=card(tall?60:32);
        auto name=c::label(row,title,tall?224:78);lv_obj_set_pos(name,8,5);
        lv_obj_set_style_text_color(name,ui::theme::muted(),0);
        auto text=c::label(row,value,tall?224:140);
        if(tall) {lv_obj_set_pos(text,8,29);lv_obj_set_height(text,26);}
        else {lv_obj_align(text,LV_ALIGN_RIGHT_MID,-8,0);lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_RIGHT,0);}
        return text;
    };
    auto note=[&](const char *text) {
        auto label=c::label(v.body,text,224);lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(label,ui::theme::muted(),0);
    };
    char text[192];esp_chip_info_t chip{};esp_chip_info(&chip);
    auto hero=card(80);
    auto logo=c::label(hero,"YUMO",112);lv_obj_set_pos(logo,10,8);
    lv_obj_set_style_text_font(logo,&lv_font_montserrat_24,0);lv_obj_set_style_text_color(logo,ui::theme::accent(),0);
    auto name=c::label(hero,"雨墨",66);lv_obj_align(name,LV_ALIGN_TOP_RIGHT,-8,12);
    std::snprintf(text,sizeof(text),"ESP32-S3 · %u核 · %u个应用",unsigned(chip.cores),unsigned(ui::WindowManager::instance().apps().size()));
    auto model=c::label(hero,text,222);lv_obj_set_pos(model,9,46);
    heading(LV_SYMBOL_REFRESH,"实时状态");
    v.aboutUptime=field("运行时间","");v.aboutCpu=field("CPU","");
    buildStorageOverview();
    v.aboutEvents=field("丢弃事件","");
    heading(LV_SYMBOL_BATTERY_FULL,"电池与电源");
    v.aboutPower=field("估算电量 / 电压","",true);v.aboutSupply=field("当前供电","",true);
    const char *reset="其他";
    switch(esp_reset_reason()) {case ESP_RST_POWERON:reset="上电";break;case ESP_RST_SW:reset="软件重启";break;case ESP_RST_DEEPSLEEP:reset="深睡唤醒";break;case ESP_RST_BROWNOUT:reset="低电压复位";break;case ESP_RST_PANIC:reset="异常复位";break;case ESP_RST_INT_WDT:case ESP_RST_TASK_WDT:case ESP_RST_WDT:reset="看门狗复位";break;default:break;}
    field("启动原因",reset);note("电量按电压估算；断电后需校时。");
    heading(LV_SYMBOL_WIFI,"连接与存储");
    v.aboutWifi=field("Wi-Fi","",true);v.aboutWifiIp=field("本机IP","");
    v.aboutBluetooth=field("蓝牙","",true);v.aboutBluetoothAddress=field("蓝牙设备地址","",true);
    v.aboutMotion=field("运动感应","");v.aboutSd=field("SD卡","");
    v.aboutUsb=field("USB模式","");v.aboutCast=field("画面投放","",true);
    heading(LV_SYMBOL_SETTINGS,"硬件规格");
    field("屏幕","1.54英寸 · 240×240",true);field("显示 / 触控","RGB565 · CST816S",true);
    field("运动芯片","QMI8658 · 六轴",true);field("音频","双麦 ES7210 / ES8311",true);
    field("Wi-Fi","2.4GHz · 热点最多4台",true);field("蓝牙功能","BLE键鼠 / 单个手柄或键盘",true);
    std::snprintf(text,sizeof(text),"LX7 · %u核 · 修订%u.%u",unsigned(chip.cores),unsigned(chip.revision/100),unsigned(chip.revision%100));
    field("处理器",text,true);note("不支持蓝牙音箱及直连HDMI。");
    heading(LV_SYMBOL_FILE,"固件与分区");
    const auto *desc=esp_app_get_description();
    field("程序",desc->project_name,true);field("版本",desc->version,true);
    std::snprintf(text,sizeof(text),"%.15s %.15s",desc->date,desc->time);field("编译时间",text,true);
    std::snprintf(text,sizeof(text),"%02x%02x%02x%02x%02x%02x",desc->app_elf_sha256[0],desc->app_elf_sha256[1],desc->app_elf_sha256[2],desc->app_elf_sha256[3],desc->app_elf_sha256[4],desc->app_elf_sha256[5]);
    field("固件标识",text,true);field("ESP-IDF",esp_get_idf_version(),true);
    uint8_t mac[6]{};
    if(esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK){std::snprintf(text,sizeof(text),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);field("Wi-Fi MAC",text,true);}
    note("容量单位为二进制。SD按需读取，无法自动检测插卡。");
}
void Settings::refreshAbout() {
    constexpr uint32_t caps[]={MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT};
    const auto seconds=uint64_t(esp_timer_get_time()/1000000);char text[192];
    std::snprintf(text,sizeof(text),"%llud %02u:%02u:%02u",static_cast<unsigned long long>(seconds/86400),unsigned(seconds/3600%24),unsigned(seconds/60%60),unsigned(seconds%60));c::setText(v.aboutUptime,text);
    std::snprintf(text,sizeof(text),"%u MHz",unsigned(esp_clk_cpu_freq()/1000000));c::setText(v.aboutCpu,text);
    refreshStorageOverview();
    std::snprintf(text,sizeof(text),"最低 / 连续 %u / %u KiB",unsigned(heap_caps_get_minimum_free_size(caps[0])/1024),unsigned(heap_caps_get_largest_free_block(caps[0])/1024));c::setText(v.aboutMemoryDetail,text);
    std::snprintf(text,sizeof(text),"%lu",static_cast<unsigned long>(sys::EventBus::dropped()));c::setText(v.aboutEvents,text);
    const auto b=sys::Battery::snapshot();
    if(b.valid)std::snprintf(text,sizeof(text),"%d%% · %d.%03d V",b.percent,b.millivolts/1000,b.millivolts%1000);
    else std::snprintf(text,sizeof(text),"暂不可用");
    c::setText(v.aboutPower,text);
    const auto supply=sys::Battery::supplyState();
    c::setText(v.aboutSupply,supply==sys::supply::State::External?(b.chargingKnown&&b.charging?"外接电源 · 充电中":"外接电源"):
        supply==sys::supply::State::BatteryConfirmed?"已确认使用电池":
        supply==sys::supply::State::BatteryInferred?"无外电信号 · 按电池省电":
        supply==sys::supply::State::Protected?"手动插电保护":"待判定 · 暂按插电保护");
    const auto wifi=sys::Connectivity::snapshot();const auto bt=sys::SystemService::bluetooth();
    const char *wifiState=wifi.transitioning?"切换中":wifi.state==sys::WifiState::Failed?"连接失败":!wifi.enabled?"已关闭":wifi.state==sys::WifiState::Searching?"查找中":wifi.state==sys::WifiState::Connecting?"连接中":wifi.state==sys::WifiState::Connected?"已连接":"未连接";
    std::snprintf(text,sizeof(text),"%s%s%s",wifiState,wifi.state==sys::WifiState::Connected?" · ":"",wifi.state==sys::WifiState::Connected?wifi.ssid.c_str():"");c::setText(v.aboutWifi,text);
    c::setText(v.aboutWifiIp,wifi.state==sys::WifiState::Connected?wifi.ip.c_str():"--");
    const char *ble=bt.hasError()?"连接异常":!bt.enabled?"已关闭":bt.state==sys::BleHidState::Starting?"启动中":bt.state==sys::BleHidState::Connecting?"连接中":bt.peer.connected?"已连接":"未连接";
    std::snprintf(text,sizeof(text),"%s%s%s",ble,bt.peer.connected?" · ":"",bt.peer.connected?(bt.peer.name[0]?bt.peer.name:"未提供名称"):"");c::setText(v.aboutBluetooth,text);
    char address[18]="--";if(bt.peer.connected)sys::BleHid::formatAddr(bt.peer,address,sizeof(address));c::setText(v.aboutBluetoothAddress,address);
    c::setText(v.aboutMotion,sys::Qmi8658::enabled()?"已开启":"已关闭");
    const char *sd="按需读取";
    switch(core::sd::state()){case core::sd::State::Usb:sd="电脑独占";break;case core::sd::State::Ejected:sd="已安全弹出";break;case core::sd::State::Reading:sd="读取中";break;case core::sd::State::Ejecting:sd="弹出中";break;case core::sd::State::Failed:sd="操作失败";break;default:break;}
    c::setText(v.aboutSd,sd);
    c::setText(v.aboutUsb,sys::UsbStorage::transitioning()?"切换中":sys::UsbStorage::enabled()?"SD卡读卡器":"开发与烧录");
    c::setText(v.aboutCast,sys::DisplayCast::statusText());
}
} // namespace apps
