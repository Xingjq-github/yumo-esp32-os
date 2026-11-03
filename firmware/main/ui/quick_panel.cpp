#include "quick_panel.hpp"
#include "quick_panel_model.hpp"
#include "window_mgr.hpp"
#include "compact_ui.hpp"
#include "text_input.hpp"
#include "external_keyboard.hpp"
#include "bluetooth_pairing.hpp"
#include "theme.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include "core/connectivity.hpp"
#include "core/power_mgr.hpp"
#include "core/button_mgr.hpp"
#include "core/usb_storage.hpp"
#include "core/sd_media.hpp"
#include "core/storage_browser.hpp"
#include "core/audio_recorder.hpp"
#include "core/game_audio.hpp"
#include "core/mp3_player.hpp"
#include "core/fc_emulator.hpp"
#include "core/password_vault.hpp"
#include <algorithm>
#include <cstdio>
namespace ui {
namespace {
namespace c=compact;
using L=quick::Layout;
enum class Action {None,Wifi,Bluetooth,Motion,Usb,Sleep,ConfirmUsb,Dark,Deep,Cancel};
lv_indev_t *pointer=nullptr;
lv_indev_read_cb_t originalRead=nullptr;
lv_obj_t *sheet=nullptr,*dialog=nullptr,*toast=nullptr,*pressedObject=nullptr;
lv_obj_t *brightness=nullptr,*volume=nullptr,*values[2]{},*tiles[3]{},*usb=nullptr,*usbText=nullptr;
quick::Swipe swipe;
bool openPending=false,closePending=false,usbTarget=false,usbWasBusy=false,deepWaiting=false,blockPageEvents=false;
Action pending=Action::None;
uint32_t nextRefresh=0,toastAt=0,resultRevision[3]{};

bool available() {
    return sys::Power::isScreenOn()&&!sys::Power::sleepPending()&&!BluetoothPairing::root()&&
        WindowManager::instance().allowsQuickPanel();
}
void pressedDeleted(lv_event_t *){pressedObject=nullptr;}
void forgetPressed() {
    if(pressedObject)lv_obj_remove_event_cb(pressedObject,pressedDeleted);
    pressedObject=nullptr;
}
void observe(lv_event_t *e) {
    if(lv_event_get_code(e)==LV_EVENT_PRESSED) {
        forgetPressed();pressedObject=static_cast<lv_obj_t *>(lv_event_get_param(e));
        if(pressedObject)lv_obj_add_event_cb(pressedObject,pressedDeleted,LV_EVENT_DELETE,nullptr);
    } else if(lv_event_get_code(e)==LV_EVENT_RELEASED||lv_event_get_code(e)==LV_EVENT_INDEV_RESET)forgetPressed();
}
void cancelPointer() {
    // Clear app-specific press latches as well as LVGL's pressed/scroll state.
    auto object=pressedObject;forgetPressed();
    if(object)lv_obj_send_event(object,LV_EVENT_PRESS_LOST,pointer);
    if(pointer)lv_indev_reset(pointer,nullptr);
}
void readPointer(lv_indev_t *input,lv_indev_data_t *data) {
    originalRead(input,data);
    bool eligible=sheet?dialog==nullptr:available();
    if(!swipe.down&&!sheet&&eligible) {
        // A boot cover, pairing dialog or dropdown list owns its own gestures.
        if(lv_indev_search_obj(lv_layer_top(),&data->point))eligible=false;
    }
    const auto route=swipe.update(data->state==LV_INDEV_STATE_PRESSED,data->point.x,data->point.y,lv_tick_get(),eligible,sheet!=nullptr);
    if(route==quick::Route::Hold)data->point={swipe.x,swipe.y};
    if(route==quick::Route::Open||route==quick::Route::Close) {
        cancelPointer();openPending=route==quick::Route::Open;closePending=route==quick::Route::Close;
    }
    if(route==quick::Route::Open||route==quick::Route::Close||route==quick::Route::Consume)
        data->state=LV_INDEV_STATE_RELEASED;
}
lv_obj_t *text(lv_obj_t *parent,const char *s,int x,int y,int w) {
    auto label=c::label(parent,s,w);lv_obj_set_pos(label,x,y);return label;
}
void sliderIcon(lv_obj_t *row,bool sound) {
    if(sound) {
        auto icon=c::label(row,LV_SYMBOL_VOLUME_MAX,28);
        lv_obj_set_style_text_font(icon,&lv_font_montserrat_24,0);
        lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(icon,LV_ALIGN_LEFT_MID,6,0);
        return;
    }
    // Small vector sun: no image buffer or additional font glyph is needed.
    auto icon=lv_obj_create(row);lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon,24,24);lv_obj_align(icon,LV_ALIGN_LEFT_MID,8,0);
    lv_obj_set_clickable(icon,false);lv_obj_set_scrollable(icon,false);
    lv_obj_add_event_cb(icon,[](lv_event_t *e) {
        lv_area_t area;lv_obj_get_coords(lv_event_get_target_obj(e),&area);
        auto layer=lv_event_get_layer(e);
        lv_draw_rect_dsc_t ring;lv_draw_rect_dsc_init(&ring);
        ring.bg_opa=LV_OPA_TRANSP;ring.border_color=theme::fg();ring.border_width=2;ring.radius=LV_RADIUS_CIRCLE;
        lv_area_t center={area.x1+8,area.y1+8,area.x1+15,area.y1+15};lv_draw_rect(layer,&ring,&center);
        lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
        line.color=theme::fg();line.width=2;line.round_start=line.round_end=1;
        static constexpr int8_t rays[][4]={{12,1,12,4},{19,4,17,6},{20,12,23,12},{18,18,20,20},
            {12,20,12,23},{4,20,6,18},{1,12,4,12},{4,4,6,6}};
        for(const auto &ray:rays) {
            line.p1.x=area.x1+ray[0];line.p1.y=area.y1+ray[1];
            line.p2.x=area.x1+ray[2];line.p2.y=area.y1+ray[3];lv_draw_line(layer,&line);
        }
    },LV_EVENT_DRAW_MAIN,nullptr);
}
lv_obj_t *button(lv_obj_t *parent,const char *label,int x,int y,int w,int h,Action action) {
    auto b=c::button(parent,label,[](lv_event_t *e){
        pending=static_cast<Action>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    },reinterpret_cast<void *>(uintptr_t(action)),w,h);
    lv_obj_set_pos(b,x,y);
    // Geometry is fixed across all five themes; only the palette changes.
    lv_obj_set_style_radius(b,6,0);lv_obj_set_style_border_width(b,1,0);
    lv_obj_set_style_pad_all(b,0,0);
    lv_obj_set_gesture_bubble(b,false);
    return b;
}
void notice(const char *message) {
    if(!sheet)return;
    if(toast)lv_obj_delete(toast);
    toast=c::panel(sheet,228,66);lv_obj_set_pos(toast,6,5);
    lv_obj_set_style_bg_color(toast,theme::surface(),0);lv_obj_set_style_radius(toast,6,0);
    auto label=text(toast,message,5,3,218);lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
    toastAt=lv_tick_get();
}
void report(esp_err_t error) {
    if(error==ESP_OK)return;
    const auto message=sys::SystemService::errorText(error);
    notice(message?message:"操作失败，请稍后重试");
}
void closeDialog() {
    swipe.cancel();cancelPointer();
    if(dialog)lv_obj_delete(dialog);
    dialog=nullptr;ExternalKeyboard::rescope();
}
void prompt(bool sleeping) {
    closeDialog();
    dialog=c::panel(sheet,240,240);lv_obj_set_user_flag(dialog,0,true);
    text(dialog,sleeping?"睡眠模式":"USB连接模式",6,10,228);
    const char *message=sleeping?
        "仅熄屏：单击PWR点亮。\n深睡：保存退出、断开无线；快速双击PWR唤醒。":usbTarget?
        "切为SD读卡器？\n电脑独占SD，设备暂停读写。":
        "切回开发与烧录？\n先停止复制；强制切换后，未完成的传输可能丢失。";
    auto detail=text(dialog,message,6,45,228);lv_obj_set_height(detail,102);lv_label_set_long_mode(detail,LV_LABEL_LONG_MODE_WRAP);
    if(sleeping) {
        button(dialog,"仅熄屏",6,149,111,40,Action::Dark);
        auto deep=button(dialog,"深度睡眠",123,149,111,40,Action::Deep);
        c::disabled(deep,!sys::UsbStorage::idle());
        if(!sys::UsbStorage::idle())lv_label_set_text(detail,"手动深睡需先关闭读卡器。\n仅熄屏不影响电脑读写。");
        button(dialog,"取消",6,195,228,40,Action::Cancel);
    } else {
        button(dialog,"取消",6,195,111,40,Action::Cancel);
        button(dialog,"确认切换",123,195,111,40,Action::ConfirmUsb);
    }
    ExternalKeyboard::rescope();
}
bool storageIdle() {
    return core::storageIdle()&&core::recorder::idle()&&core::fc::idle()&&vault::idle()&&core::game_audio::idle()&&
        core::mp3::idle()&&core::mp3::storageIdle()&&!core::sd::busy();
}
void refresh() {
    const int numbers[]={sys::Settings::brightness(),sys::Settings::volume()};
    for(unsigned i=0;i<2;++i) {
        auto slider=i?volume:brightness;
        if(!lv_obj_has_state(slider,LV_STATE_PRESSED))lv_slider_set_value(slider,numbers[i],LV_ANIM_OFF);
        char value[12];std::snprintf(value,sizeof(value),"%d%%",int(lv_slider_get_value(slider)));c::setText(values[i],value);
    }
    const auto wifi=sys::Connectivity::snapshot();const auto bt=sys::SystemService::bluetooth();
    const bool hotspot=sys::Connectivity::hotspotReserved();
    const bool wifiBusy=sys::SystemService::busy(sys::ServiceDomain::Wifi)||wifi.transitioning;
    const bool btBusy=sys::SystemService::busy(sys::ServiceDomain::Bluetooth)||bt.state==sys::BleHidState::Starting;
    c::checked(tiles[0],wifi.enabled);c::disabled(tiles[0],wifiBusy||hotspot);
    c::checked(tiles[1],bt.enabled);c::disabled(tiles[1],btBusy);
    const bool motion=sys::Settings::motionSensorOn();
    const bool motionBusy=sys::SystemService::busy(sys::ServiceDomain::System);
    c::checked(tiles[2],motion);c::disabled(tiles[2],motionBusy);
    // Icon-only tiles: keep on/off backgrounds and signal radio status by outline.
    auto outline=[](lv_obj_t *tile,bool pending,bool failed,bool connected) {
        const auto color=failed?theme::danger():pending?theme::warning():
            connected?theme::success():theme::edge();
        // Compare the declared default value, not an in-flight state transition.
        lv_style_value_t previous;
        if(lv_obj_get_local_style_prop(tile,LV_STYLE_BORDER_COLOR,&previous,0)!=LV_STYLE_RES_FOUND ||
            !lv_color_eq(previous.color,color))lv_obj_set_style_border_color(tile,color,0);
    };
    outline(tiles[0],wifiBusy||hotspot,wifi.state==sys::WifiState::Failed,wifi.state==sys::WifiState::Connected);
    outline(tiles[1],btBusy,bt.hasError(),bt.peer.connected);
    outline(tiles[2],motionBusy,false,false);
    const bool busy=sys::UsbStorage::transitioning(),on=sys::UsbStorage::enabled();
    c::checked(usb,on);c::disabled(usb,busy||(!on&&!storageIdle()));
    c::setText(usbText,busy?"USB切换中":on?"USB读卡器":!sys::UsbStorage::idle()?"恢复失败":!storageIdle()?"USB忙":"USB开发");
    if(usbWasBusy&&!busy)notice(sys::UsbStorage::statusText());
    usbWasBusy=busy;
    for(unsigned i=0;i<3;++i) {
        const auto result=sys::SystemService::result(static_cast<sys::ServiceDomain>(i));
        if(result.revision!=resultRevision[i]) {
            resultRevision[i]=result.revision;
            if(deepWaiting && result.action==sys::ServiceAction::DeepSleep && result.error!=ESP_OK) {
                deepWaiting=false;prompt(true);
            }
            if(result.action==sys::ServiceAction::WifiEnable||result.action==sys::ServiceAction::BluetoothEnable||
                result.action==sys::ServiceAction::Motion||result.action==sys::ServiceAction::DeepSleep)report(result.error);
        }
    }
}
void build() {
    TextInput::hide();
    sheet=c::panel(lv_layer_top(),240,240);lv_obj_set_user_flag(sheet,0,true);
    lv_obj_set_gesture_bubble(sheet,false);
    for(unsigned i=0;i<2;++i) {
        const int y=L::SliderY+int(i)*(L::SliderH+L::SliderGap);
        auto row=c::panel(sheet,228,L::SliderH);lv_obj_set_pos(row,6,y);
        lv_obj_set_style_bg_color(row,theme::surface(),0);lv_obj_set_style_radius(row,6,0);
        sliderIcon(row,i!=0);
        auto slider=lv_slider_create(row);lv_obj_remove_style_all(slider);
        lv_obj_set_pos(slider,43,(L::SliderH-14)/2);lv_obj_set_size(slider,138,14);
        // Keep the knob clear of the icon and the full-width percentage at both ends.
        lv_obj_set_style_pad_hor(slider,3,LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,0);lv_obj_set_style_bg_color(slider,theme::edge(),0);lv_obj_set_style_radius(slider,7,0);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_bg_color(slider,theme::accent(),LV_PART_INDICATOR);lv_obj_set_style_radius(slider,7,LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_KNOB);lv_obj_set_style_bg_color(slider,theme::fg(),LV_PART_KNOB);lv_obj_set_style_radius(slider,LV_RADIUS_CIRCLE,LV_PART_KNOB);lv_obj_set_style_pad_all(slider,2,LV_PART_KNOB);
        lv_obj_set_ext_click_area(slider,17);lv_obj_set_gesture_bubble(slider,false);
        lv_slider_set_range(slider,i?0:5,100);
        if(i)volume=slider;else brightness=slider;
        values[i]=text(row,"",190,0,36);lv_obj_set_style_text_font(values[i],&lv_font_montserrat_14,0);lv_obj_set_style_text_align(values[i],LV_TEXT_ALIGN_RIGHT,0);
        lv_obj_align(values[i],LV_ALIGN_RIGHT_MID,-2,0);
        lv_obj_add_event_cb(slider,[](lv_event_t *e){
            auto target=lv_event_get_target_obj(e);const auto value=lv_slider_get_value(target);
            sys::Power::activity();
            if(target==brightness){sys::Settings::setBrightness(value);sys::Power::applyBrightness(value);}
            else sys::SystemService::setVolume(value);
            char text[12];std::snprintf(text,sizeof(text),"%d%%",int(value));c::setText(values[target==volume],text);
        },LV_EVENT_VALUE_CHANGED,nullptr);
    }
    const char *icons[]={LV_SYMBOL_WIFI,LV_SYMBOL_BLUETOOTH,LV_SYMBOL_REFRESH};
    const Action actions[]={Action::Wifi,Action::Bluetooth,Action::Motion};
    for(unsigned i=0;i<3;++i) {
        tiles[i]=button(sheet,"",6+int(i)*(L::TileW+L::Gap),L::TileY,L::TileW,L::TileH,actions[i]);
        auto icon=c::label(tiles[i],icons[i]);
        lv_obj_set_style_text_font(icon,&lv_font_montserrat_24,0);lv_obj_center(icon);
    }
    usb=button(sheet,"",6,L::ActionY,L::ActionW,L::ActionH,Action::Usb);
    usbText=text(usb,"",3,8,103);lv_obj_set_style_text_align(usbText,LV_TEXT_ALIGN_CENTER,0);
    button(sheet,LV_SYMBOL_POWER " 睡眠",123,L::ActionY,L::ActionW,L::ActionH,Action::Sleep);
    auto handle=c::panel(sheet,42,3);lv_obj_set_pos(handle,99,230);
    lv_obj_set_style_bg_color(handle,theme::muted(),0);lv_obj_set_style_radius(handle,2,0);
    lv_obj_set_clickable(handle,false);
    for(unsigned i=0;i<3;++i)resultRevision[i]=sys::SystemService::result(static_cast<sys::ServiceDomain>(i)).revision;
    usbWasBusy=sys::UsbStorage::transitioning();
    blockPageEvents=true;sys::Buttons::setSystemPanel(true);ExternalKeyboard::rescope();sys::Power::activity();nextRefresh=0;
    refresh();
}
}
void QuickPanel::init(lv_indev_t *input) {
    if(pointer||!input)return;
    originalRead=lv_indev_get_read_cb(input);if(!originalRead)return;
    pointer=input;lv_indev_set_read_cb(pointer,readPointer);
    for(auto event:{LV_EVENT_PRESSED,LV_EVENT_RELEASED,LV_EVENT_INDEV_RESET})lv_indev_add_event_cb(pointer,observe,event,nullptr);
}
bool QuickPanel::visible(){return sheet||openPending;}
lv_obj_t *QuickPanel::root(){return sheet;}
void QuickPanel::close() {
    if(visible())blockPageEvents=true;
    openPending=closePending=false;pending=Action::None;swipe.cancel();
    deepWaiting=false;
    if(!sheet)return;
    cancelPointer();sys::Buttons::setSystemPanel(false);ExternalKeyboard::rescope();
    lv_obj_delete(sheet);sheet=dialog=toast=nullptr;brightness=volume=usb=usbText=nullptr;
    for(auto &v:tiles)v=nullptr;
    for(auto &v:values)v=nullptr;
}
bool QuickPanel::back() {
    if(!visible())return false;
    if(deepWaiting)return true;
    if(dialog){closeDialog();pending=Action::None;}else close();
    return true;
}
bool QuickPanel::allowEvent(const sys::Event &event) {
    return !(visible()||blockPageEvents)||(event.type!=sys::EventType::PreviousPage&&event.type!=sys::EventType::NextPage);
}
void QuickPanel::poll() {
    blockPageEvents=visible();
    if(!available()) {
        // Discard partial swipes across blackout, while excluded lit apps
        // retain their own press/move/release gesture sequence.
        if(visible() || !sys::Power::isScreenOn() || sys::Power::sleepPending())close();
        return;
    }
    if(closePending){close();return;}
    if(openPending){openPending=false;if(!sheet)build();}
    if(!sheet)return;
    const auto action=pending;pending=Action::None;
    esp_err_t error=ESP_OK;
    switch(action) {
    case Action::Wifi:error=sys::SystemService::request(sys::ServiceAction::WifiEnable,!sys::Connectivity::snapshot().enabled);break;
    case Action::Bluetooth:error=sys::SystemService::request(sys::ServiceAction::BluetoothEnable,!sys::SystemService::bluetooth().enabled);break;
    case Action::Motion:error=sys::SystemService::request(sys::ServiceAction::Motion,!sys::Settings::motionSensorOn());break;
    case Action::Usb:usbTarget=!sys::UsbStorage::enabled();if(usbTarget&&!storageIdle())notice("请先结束当前SD或音频任务");else prompt(false);break;
    case Action::Sleep:prompt(true);break;
    case Action::ConfirmUsb:
        closeDialog();
        if(usbTarget&&!storageIdle())notice("请先结束当前SD或音频任务");
        else {error=sys::UsbStorage::setEnabled(usbTarget);if(error==ESP_OK)notice("正在切换USB模式");}
        break;
    case Action::Dark:close();sys::Power::screenOff();return;
    case Action::Deep:
        if(!sys::UsbStorage::idle())notice("请先切回开发与烧录");
        else {
            error=sys::SystemService::request(sys::ServiceAction::DeepSleep);
            if(error==ESP_OK) {
                deepWaiting=true;closeDialog();
                dialog=c::panel(sheet,240,240);lv_obj_set_user_flag(dialog,0,true);
                text(dialog,"正在准备深度睡眠…",6,100,228);
                ExternalKeyboard::rescope();
            }
        }
        break;
    case Action::Cancel:closeDialog();break;
    default:break;
    }
    report(error);
    const auto now=lv_tick_get();
    if(toast&&uint32_t(now-toastAt)>=3000){lv_obj_delete(toast);toast=nullptr;}
    if(action!=Action::None||int32_t(now-nextRefresh)>=0){nextRefresh=now+250;refresh();}
}
}
