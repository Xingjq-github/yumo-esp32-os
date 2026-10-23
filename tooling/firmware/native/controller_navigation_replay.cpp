// Production dispatcher/editors/clock hooks; only radio, power and service IO are fake.
#include "lvgl.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <vector>
#include "core/keyboard_input.hpp"
#include "core/ble_keyboard_client.hpp"
#include "core/hid_input_report.hpp"
#include "core/hid_gamepad_report.hpp"
#include "core/hid_iine_profile.hpp"
#include "core/ble_hid.hpp"
#include "core/event_bus.hpp"
#include "core/system_service.hpp"
#include "ui/external_keyboard.hpp"
#include "ui/text_input.hpp"
#include "ui/bluetooth_pairing.hpp"
#include "ui/bluetooth_identity.hpp"
#include "ui/quick_panel.hpp"
#include "ui/compact_ui.hpp"
#include "ui/control_states.h"
#define private public
#include "ui/window_mgr.hpp"
#include "apps/watchface.hpp"
#undef private

static unsigned checks=0,clicks=0,backs=0,rawPresses=0,wakes=0,commits=0,faceRenders=0;
static bool connected=true,hmode=true,lit=true,sleeping=false;
static lv_obj_t *panel=nullptr;
static sys::hidinput::Keys snapshot;
static std::deque<sys::hidinput::Keys> reports;
static std::deque<sys::Event> events;
static sys::BluetoothSnapshot pairingState;
static unsigned answers=0;static uint32_t answeredGeneration=0;
static bool accepted=false;static int answerResult=ESP_OK;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGE(...)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void *heap_caps_malloc(size_t n,int){return std::malloc(n);}
#include "ble_transport.inc"
#define ESP_LOGI(...)
#define ESP_LOGW(...)
struct StaticQueue_t{};
using QueueHandle_t=StaticQueue_t *;
constexpr int pdTRUE=1;
static QueueHandle_t xQueueCreateStatic(size_t,size_t,uint8_t *,StaticQueue_t *q){return q;}
static int xQueueSend(QueueHandle_t,const void *value,int){if(events.size()==24)return 0;events.push_back(*static_cast<const sys::Event *>(value));return pdTRUE;}
static int xQueueReceive(QueueHandle_t,void *value,int){if(events.empty())return 0;*static_cast<sys::Event *>(value)=events.front();events.pop_front();return pdTRUE;}
namespace sys {
bool BleHid::keyboardConnected(){return ::connected&&BleKeyboardClient::keyboardReady();}
BleBond BleHid::peer(){BleBond p;std::snprintf(p.name,sizeof(p.name),"%s",fake::name);return p;}
void BleHid::keyboardDiscoveryFinished(bool,int){}
void BleHid::formatAddr(const BleBond &,char *out,size_t size){std::snprintf(out,size,"00:00:00:00:00:01");}
bool KeyboardInput::pop(hidinput::Keys &keys){if(reports.empty())return false;keys=reports.front();reports.pop_front();return true;}
hidinput::Keys KeyboardInput::snapshot(uint32_t &revision){revision=1;return ::snapshot;}
void KeyboardInput::publish(const hidinput::Keys &keys){::snapshot=keys;reports.push_back(keys);}
void KeyboardInput::reset(){::snapshot={};reports.clear();}
BluetoothSnapshot SystemService::bluetooth(){return pairingState;}
esp_err_t SystemService::answerPairing(bool accept,uint32_t generation){++answers;accepted=accept;answeredGeneration=generation;return answerResult;}
class Power {public:static bool isScreenOn(){return lit;}static bool sleepPending(){return sleeping;}static void screenOn(){lit=true;++wakes;}};
}
namespace ui {
namespace { bool blockPageEvents=false; }
class HomeScreen {public:static bool cancelArrange(){return false;}};
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::closeActive(){ExternalKeyboard::resetFocus();m_active.reset();}
void WindowManager::showTop(){}
bool QuickPanel::visible(){return panel;}
lv_obj_t *QuickPanel::root(){return panel;}
bool QuickPanel::back(){if(!panel)return false;lv_obj_delete(panel);panel=nullptr;ExternalKeyboard::rescope();return true;}
}
namespace apps {
constexpr int kFaceCount=4;
const char *Watchface::icon()const{return nullptr;}
void Watchface::onCreate(lv_obj_t *){}void Watchface::onShow(){m_visible=true;}void Watchface::onHide(){m_visible=false;}
void Watchface::onDestroy(){}void Watchface::onPowerSaving(bool){}
void Watchface::applyFace(){++faceRenders;}void Watchface::refresh(){}
}
#include "compact_ui.inc"
#include "text_input.inc"
#include "bluetooth_pairing.inc"
#include "external_keyboard.inc"
#include "window_mgr.inc"
#include "watchface.inc"
#include "quick_panel.inc"
#include "event_bus.inc"
#include "ble_client.inc"
#include "ble_discovery.inc"
#include "h_fixture.inc"

static void connectMode(bool controller){
    hmode=controller;std::strcpy(fake::name,"HID Keyboard");
    discover(hMap,controller?6:1);
    if(controller)for(uint8_t id=1;id<=6;++id)subscribe(id);else subscribe(2);
    CHECK(sys::BleKeyboardClient::iineKeyboard()==controller);
}

static uint8_t framebuffer[240*240*2];
static void frame(unsigned ms=25,bool read=true){
    lv_tick_inc(ms);ui::ExternalKeyboard::poll();sys::EventBus::dispatch(8,ui::QuickPanel::allowEvent);
    if(read){lv_indev_read(ui::input);lv_timer_handler();}
    lv_obj_update_layout(lv_screen_active());
}
static void report(unsigned usage){const uint8_t bytes[]={0,0,uint8_t(usage),0,0,0,0,0};sys::BleKeyboardClient::notification(12,hmode?14:6,bytes,sizeof(bytes));}
static void tap(unsigned usage){report(usage);report(0);for(unsigned i=0;i<4;++i)frame();}
static void raw(const sys::hidinput::Keys &keys,void *){if(std::any_of(keys.bits.begin(),keys.bits.end(),[](uint8_t v){return v!=0;}))++rawPresses;}
static lv_obj_t *scene(){
    ui::TextInput::hide();ui::QuickPanel::back();ui::ExternalKeyboard::resetFocus();
    auto &wm=ui::WindowManager::instance();wm.m_active.reset();lv_obj_clean(lv_screen_active());
    auto root=ui::compact::panel(lv_screen_active(),240,240);wm.m_home=root;
    connected=lit=true;connectMode(true);sleeping=false;reports.clear();snapshot={};events.clear();frame();return root;
}
static lv_obj_t *button(lv_obj_t *root,int x,int y,int w=60,int h=36){
    auto b=ui::compact::button(root,"",[](lv_event_t *){++clicks;},nullptr,w,h);lv_obj_set_pos(b,x,y);return b;
}
static lv_obj_t *focused(){return lv_group_get_focused(ui::group);}
static size_t freeMemory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}

int main(){
    CHECK(sys::EventBus::init());
    sys::EventBus::subscribe(sys::EventType::BackRequested,[](const sys::Event &){++backs;ui::WindowManager::instance().goBack();});
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    auto pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,[](lv_indev_t *,lv_indev_data_t *d){d->state=LV_INDEV_STATE_RELEASED;});
    ui::TextInput::init(pointer);ui::ExternalKeyboard::init();
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);
        auto root=scene();auto a=button(root,8,8),b=button(root,150,8),c=button(root,8,140),d=button(root,150,140);
        auto hidden=button(root,80,8);lv_obj_set_hidden(hidden,true);
        auto disabled=button(root,80,55);lv_obj_add_state(disabled,LV_STATE_DISABLED);
        auto indicator=button(root,80,100);lv_obj_set_clickable(indicator,false);
        auto hiddenParent=ui::compact::panel(root,40,40);button(hiddenParent,0,0);lv_obj_set_hidden(hiddenParent,true);
        frame(125);CHECK(focused()==a);CHECK(lv_group_get_obj_count(ui::group)==4);
        CHECK(lv_obj_has_state(a,LV_STATE_FOCUS_KEY));
        tap(0x4f);CHECK(focused()==b);tap(0x51);CHECK(focused()==d);tap(0x50);CHECK(focused()==c);tap(0x52);CHECK(focused()==a);
        tap(0x50);CHECK(focused()==a); // No wrap to an unrelated opposite edge.
        auto n=clicks;report(0x28);frame();CHECK(clicks==n+1);frame(600);frame(100);CHECK(clicks==n+1);report(0);frame();
        auto backCount=backs;report(0x2c);frame();frame(700);CHECK(backs==backCount+1);report(0);frame();
        // A must be consumed before a later arrow can change its target.
        lv_group_focus_obj(a);n=clicks;report(0x28);report(0);report(0x4f);report(0);frame(25,false);CHECK(ui::count==2);
        frame(25,false);CHECK(focused()==a&&clicks==n);lv_indev_read(ui::input);CHECK(clicks==n+1);
        frame();CHECK(focused()==b);frame();
        // Direction repeats, unlike A/B. A queued release stops the repeat.
        lv_group_focus_obj(a);report(0x51);frame();CHECK(focused()==c);frame(510);CHECK(focused()==c);
        report(0);frame(100);CHECK(!ui::repeatUsage);
        // H bypasses FC raw capture; ordinary keyboards retain it.
        ui::ExternalKeyboard::capture(raw,nullptr);frame();n=rawPresses;tap(0x28);CHECK(rawPresses==n);
        connectMode(false);tap(0x04);CHECK(rawPresses==n+1);connectMode(true);ui::ExternalKeyboard::resetFocus();frame();
        // C-mode sends gamepad reports, with no keyboard UI channel.
        connected=false;report(0x4f);frame();CHECK(!lv_group_get_obj_count(ui::group));
        connected=true;report(0x28);frame();n=clicks;frame(600);CHECK(clicks==n);
        report(0);frame();tap(0x28);CHECK(clicks==n+1); // Reconnect-held key requires neutral.
        lit=false;n=clicks;auto wakeCount=wakes;report(0x28);frame();CHECK(lit&&wakes==wakeCount+1&&clicks==n);
        frame(700);report(0);frame();tap(0x28);CHECK(clicks==n+1);
        sleeping=true;frame();CHECK(!lv_group_get_obj_count(ui::group));sleeping=false;frame();
        // Off-screen list elements remain reachable and are scrolled into view.
        root=scene();auto list=ui::compact::panel(root,240,170,true);std::vector<lv_obj_t *> rows;
        for(int i=0;i<8;++i)rows.push_back(button(list,0,i*45,220,40));frame(125);
        for(int i=1;i<8;++i){tap(0x51);CHECK(focused()==rows[i]);}
        lv_area_t bounds;lv_obj_get_coords(rows.back(),&bounds);CHECK(bounds.y1>=0&&bounds.y2<170);
        // Dropdown uses native preview/commit/cancel, and B closes one layer.
        root=scene();auto dropdown=ui::compact::dropdown(root,"zero\none\ntwo",160);button(root,0,70);frame(125);
        tap(0x28);CHECK(lv_dropdown_is_open(dropdown));tap(0x51);backCount=backs;tap(0x2c);
        CHECK(!lv_dropdown_is_open(dropdown)&&lv_dropdown_get_selected(dropdown)==0&&backs==backCount);
        tap(0x28);tap(0x51);tap(0x28);CHECK(!lv_dropdown_is_open(dropdown)&&lv_dropdown_get_selected(dropdown)==1);
        // Slider navigation and editing are separate. Completion sends RELEASED.
        root=scene();auto slider=lv_slider_create(root);lv_obj_set_size(slider,180,20);lv_obj_set_pos(slider,15,15);auto next=button(root,0,70);
        lv_obj_add_event_cb(slider,[](lv_event_t *){++commits;},LV_EVENT_RELEASED,nullptr);frame(125);
        tap(0x51);CHECK(focused()==next&&lv_slider_get_value(slider)==0);tap(0x52);tap(0x28);
        CHECK(lv_group_get_editing(ui::group));tap(0x4f);CHECK(focused()==slider&&lv_slider_get_value(slider)==1);
        n=commits;backCount=backs;tap(0x2c);CHECK(!lv_group_get_editing(ui::group)&&commits==n+1&&backs==backCount);
        // All supported native controls, including H letter aliases.
        root=scene();auto sw=lv_switch_create(root);lv_obj_set_pos(sw,5,5);
        auto checkbox=lv_checkbox_create(root);lv_obj_set_pos(checkbox,5,65);
        auto roller=lv_roller_create(root);lv_obj_set_pos(roller,130,5);lv_obj_set_size(roller,100,150);
        lv_roller_set_options(roller,"zero\none\ntwo",LV_ROLLER_MODE_NORMAL);frame(125);
        CHECK(lv_group_get_obj_count(ui::group)==3);tap(0x04);CHECK(lv_obj_has_state(sw,LV_STATE_CHECKED));
        tap(0x51);CHECK(focused()==checkbox);tap(0x28);CHECK(lv_obj_has_state(checkbox,LV_STATE_CHECKED));
        tap(0x4f);CHECK(focused()==roller);tap(0x28);tap(0x51);CHECK(lv_roller_get_selected(roller)==1);
        tap(0x05);CHECK(!lv_group_get_editing(ui::group));tap(0x50);CHECK(focused()!=roller);
        // H can enter actual on-screen text input, without inserting Enter/Space.
        root=scene();auto field=ui::compact::field(root,"","",20);frame(125);CHECK(focused()==field&&!ui::TextInput::visible());
        tap(0x28);CHECK(ui::TextInput::visible()&&lv_group_get_obj_count(ui::group)==4);
        tap(0x4f);tap(0x51);tap(0x28);CHECK(!std::strcmp(lv_textarea_get_text(field),"0"));
        tap(0x50);tap(0x28);CHECK(!std::strcmp(lv_textarea_get_text(field),""));tap(0x2c);CHECK(!ui::TextInput::visible());
        connectMode(false);ui::TextInput::show(field);CHECK(!ui::TextInput::visible());connectMode(true);
        tap(0x28);CHECK(ui::TextInput::visible());lv_obj_delete(field);frame();CHECK(!ui::TextInput::visible());
        // Replacing the focused object before LVGL reads A cancels the click.
        root=scene();a=button(root,0,0);frame(125);n=clicks;report(0x28);report(0);frame(25,false);
        lv_obj_delete(a);button(root,80,0);ui::updateFocus();lv_indev_read(ui::input);CHECK(clicks==n);frame();
        // Repeated creation/destruction does not retain stale focus objects.
        size_t baseline=0;
        for(int i=0;i<24;++i){root=scene();button(root,0,0);button(root,100,0);frame(125);tap(0x4f);tap(0x28);
            ui::ExternalKeyboard::resetFocus();lv_obj_clean(root);frame();CHECK(!lv_group_get_obj_count(ui::group));
            if(i==2)baseline=freeMemory();else if(i>2)CHECK(freeMemory()==baseline);}
    }
    // Actual clock hook and WindowManager dispatch, including cover suppression.
    auto root=scene();auto clock=std::make_unique<apps::Watchface>();auto clockPtr=clock.get();clock->setRoot(root);clock->onShow();
    ui::WindowManager::instance().m_active=std::move(clock);
    for(int i=1;i<=8;++i){tap(0x4f);CHECK(clockPtr->m_face==i%4);}
    tap(0x50);CHECK(clockPtr->m_face==3);tap(0x52);CHECK(clockPtr->m_face==3);
    clockPtr->m_tracking=true;tap(0x50);CHECK(clockPtr->m_face==2&&!clockPtr->m_tracking);
    clockPtr->onHide();tap(0x4f);CHECK(clockPtr->m_face==2);clockPtr->onShow();
    panel=ui::compact::panel(lv_layer_top(),240,240);button(panel,0,0);button(panel,100,0);frame(125);
    tap(0x4f);CHECK(clockPtr->m_face==2);tap(0x2c);CHECK(!panel);tap(0x4f);CHECK(clockPtr->m_face==3);
    // B in the pairing overlay follows the same generation-checked cancel path.
    pairingState.enabled=true;pairingState.pairing=sys::BlePairAction::Confirm;pairingState.pairGeneration=17;
    frame(125);ui::BluetoothPairing::poll();CHECK(ui::BluetoothPairing::root());frame(125);
    tap(0x4f);CHECK(clockPtr->m_face==3);auto backCount=backs;
    answerResult=ESP_ERR_TIMEOUT;tap(0x2c);CHECK(ui::BluetoothPairing::root()&&answers==1&&!accepted&&answeredGeneration==17&&backs==backCount);
    answerResult=ESP_OK;tap(0x2c);CHECK(!ui::BluetoothPairing::root()&&answers==2&&backs==backCount);
    CHECK(faceRenders==11);
    ui::blockPageEvents=true;tap(0x2c);CHECK(!ui::WindowManager::instance().m_active);
    ui::blockPageEvents=false;
    std::printf("{\"passed\":true,\"checks\":%u,\"themes\":5,\"method\":\"production dispatcher, text input, pairing and clock hooks with real LVGL\"}\n",checks);
}
