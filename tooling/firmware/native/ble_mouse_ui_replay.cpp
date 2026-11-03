// Production App with real LVGL. Radio/NVS/power hardware are explicit doubles.
#include "apps/bt_keyboard_mouse.hpp"
#include "core/ble_hid.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "core/system_service.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

static unsigned checks=0,scenarios=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace fake {
struct Move {int32_t x,y,w;uint8_t buttons;uintptr_t link;};
struct Key {uint8_t code,mods;};
struct Subscription {sys::EventType type;sys::EventHandler handler;};
bool screen=true,connected=true;
bool changeOnSend=false;
uintptr_t link=1;
int error=ESP_OK,mouseError=ESP_OK,keyError=ESP_OK,cancelMouse=0,cancelKeys=0;
unsigned snapshots=0,linkReads=0;
uint32_t nextId=0;
std::vector<Move> moves;
std::vector<uint8_t> clicks;
std::vector<Key> keys;
std::map<uint32_t,Subscription> subscriptions;
std::unique_ptr<apps::BtKeyboardMouse> app;
}
bool sys::Power::isScreenOn(){return fake::screen;}
bool sys::Power::slowIdle(){return false;}
uint32_t sys::EventBus::subscribe(EventType type,EventHandler handler){auto id=++fake::nextId;fake::subscriptions.emplace(id,fake::Subscription{type,std::move(handler)});return id;}
void sys::EventBus::unsubscribe(uint32_t id){fake::subscriptions.erase(id);}
void sys::EventBus::publish(const Event &event){auto all=fake::subscriptions;for(auto &entry:all)if(entry.second.type==event.type && fake::subscriptions.count(entry.first))entry.second.handler(event);}
bool sys::BleHid::remoteConnected(){return fake::connected;}
uintptr_t sys::BleHid::mouseLinkGeneration(){++fake::linkReads;return fake::connected?fake::link:0;}
esp_err_t sys::BleHid::sendMouse(int32_t dx,int32_t dy,int32_t wheel,uint8_t buttons,uintptr_t expectedLink){
    if(!fake::connected)return ESP_ERR_INVALID_STATE;
    if(fake::changeOnSend){++fake::link;fake::changeOnSend=false;}
    if(expectedLink && expectedLink!=fake::link)return ESP_ERR_INVALID_STATE;
    if(fake::error!=ESP_OK)return fake::error;
    fake::moves.push_back({dx,dy,wheel,buttons,fake::link});return ESP_OK;
}
esp_err_t sys::BleHid::clickMouse(uint8_t button,uintptr_t expectedLink){if(!fake::connected || (expectedLink && expectedLink!=fake::link))return ESP_ERR_INVALID_STATE;if(fake::error!=ESP_OK)return fake::error;fake::clicks.push_back(button);return ESP_OK;}
void sys::BleHid::cancelMouse(){++fake::cancelMouse;}
esp_err_t sys::BleHid::takeMouseError(){auto error=fake::mouseError;fake::mouseError=ESP_OK;return error;}
esp_err_t sys::BleHid::sendKey(uint8_t code,uint8_t mods){if(!fake::connected)return ESP_ERR_INVALID_STATE;fake::keys.push_back({code,mods});return ESP_OK;}
void sys::BleHid::cancelKeys(){++fake::cancelKeys;}
esp_err_t sys::BleHid::takeKeyError(){auto error=fake::keyError;fake::keyError=ESP_OK;return error;}
sys::BluetoothSnapshot sys::SystemService::bluetooth(){++fake::snapshots;BluetoothSnapshot s;s.enabled=true;s.state=fake::connected?BleHidState::Connected:BleHidState::Ready;s.peer.connected=fake::connected;return s;}
void ui::TextInput::bindField(lv_obj_t *){CHECK(false);}

static uint8_t pixels[240*240*2];
static lv_indev_t *touch=nullptr;
static lv_point_t point{};
static bool pressed=false;
static void advance(unsigned ms){lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void input(int x,int y,bool down,unsigned ms=20){point={x,y};pressed=down;lv_tick_inc(ms);lv_indev_read(touch);lv_obj_update_layout(lv_screen_active());}
static void press(int x,int y){input(x,y,true);}
static void move(int x,int y){input(x,y,true);}
static void release(int x,int y,unsigned ms=20){input(x,y,false,ms);}
static void tap(int x,int y){press(x,y);release(x,y);advance(20);}
static lv_obj_t *pad(){return lv_obj_get_child(fake::app->root(),4);}
static lv_obj_t *wheel(){return lv_obj_get_child(fake::app->root(),3);}
static lv_obj_t *find(lv_obj_t *root,const char *text){
    if(lv_obj_is_hidden(root))return nullptr;
    if(lv_obj_check_type(root,&lv_label_class) && !std::strcmp(lv_label_get_text(root),text))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto found=find(lv_obj_get_child(root,i),text))return found;
    return nullptr;
}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void clear(){fake::moves.clear();fake::clicks.clear();fake::keys.clear();fake::error=fake::mouseError=fake::keyError=ESP_OK;fake::changeOnSend=false;}
static void open(){
    CHECK(!fake::app);fake::screen=fake::connected=true;pressed=false;clear();++fake::link;
    auto root=ui::compact::panel(lv_screen_active(),240,240);
    fake::app=std::make_unique<apps::BtKeyboardMouse>();fake::app->setRoot(root);fake::app->onCreate(root);fake::app->onShow();advance(60);
    CHECK(fake::subscriptions.size()==1);CHECK(fake::app->powerHints()==sys::power::None);
    CHECK(!lv_obj_is_hidden(pad()) && !lv_obj_is_hidden(wheel()));
}
static void close(){
    CHECK(fake::app);pressed=false;lv_indev_reset(touch,nullptr);auto root=fake::app->root();auto before=fake::cancelMouse;
    fake::app->onHide();CHECK(fake::app->powerHints()==sys::power::None);CHECK(fake::cancelMouse>before);
    fake::app->onDestroy();fake::app.reset();lv_obj_delete(root);advance(60);CHECK(fake::subscriptions.empty());clear();
}
static long sum(unsigned axis){long result=0;for(auto &m:fake::moves)result+=axis==0?m.x:axis==1?m.y:m.w;return result;}
static void layout(){
    auto root=fake::app->root();CHECK(lv_obj_get_style_bg_opa(root,LV_PART_MAIN)==LV_OPA_COVER);
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i){auto obj=lv_obj_get_child(root,i);if(lv_obj_is_hidden(obj))continue;lv_area_t area{};lv_obj_get_coords(obj,&area);
        CHECK(area.x1>=0 && area.x2<240 && area.y1>=0 && area.y2<240);}
}
int main(){
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *data){data->point=point;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
    const auto baselineTimers=timers();
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);size_t baselineMemory=0;
        for(int cycle=0;cycle<4;++cycle){
            open();layout();
            // Large touch deltas are submitted once and never truncated to int8.
            press(20,80);move(180,200);CHECK(fake::moves.size()==1);CHECK(sum(0)==896 && sum(1)==672);release(180,200);CHECK(fake::clicks.empty());++scenarios;
            // Small increments retain fractions rather than truncating each sample.
            clear();press(30,100);for(int x=31;x<=130;++x)move(x,100);release(130,100);CHECK(fake::moves.size()==100);CHECK(sum(0)==169 && sum(1)==0);++scenarios;
            // Release-only final movement is preserved.
            clear();press(30,80);release(50,90);CHECK(fake::moves.size()==1);CHECK(sum(0)==89 && sum(1)==44);CHECK(fake::clicks.empty());++scenarios;
            // Stationary input emits neither movement nor click until release.
            clear();press(80,130);for(int i=0;i<10;++i)move(80,130);CHECK(fake::moves.empty());release(80,130);CHECK(fake::clicks.size()==1);++scenarios;
            // A tap followed by a move holds left until an ordered zero report.
            clear();advance(400);tap(80,130);press(80,130);move(90,130);CHECK(fake::moves.size()==1 && fake::moves.back().buttons==1);release(100,130);
            CHECK(fake::moves.size()==3 && fake::moves[1].buttons==1 && fake::moves.back().buttons==0);CHECK(fake::clicks.size()==1);++scenarios;
            // Ordinary double tap remains exactly two clicks, without drag.
            clear();advance(400);tap(70,120);tap(70,120);CHECK(fake::clicks.size()==2 && fake::moves.empty());++scenarios;
            clear();advance(400);press(70,120);release(70,120,300);CHECK(fake::clicks.empty());++scenarios;
            // Scroll combines whole steps, retaining residual pixels and signs.
            clear();press(217,70);move(217,209);CHECK(fake::moves.size()==1 && sum(2)==-9);move(217,210);CHECK(fake::moves.size()==2 && sum(2)==-10);
            move(217,197);CHECK(fake::moves.size()==2);move(217,196);CHECK(fake::moves.size()==3 && sum(2)==-9);release(217,182);CHECK(fake::moves.size()==4 && sum(2)==-8);++scenarios;
            // Fast disconnect/reconnect inside the 50ms poll cannot resume an old gesture.
            clear();press(70,120);move(80,120);const auto accepted=fake::moves.size();const auto cancels=fake::cancelMouse;++fake::link;
            move(100,120);release(100,120);CHECK(fake::moves.size()==accepted && fake::clicks.empty());CHECK(fake::cancelMouse>cancels);++scenarios;
            clear();press(217,70);move(217,100);++fake::link;const auto wheels=fake::moves.size();move(217,200);release(217,200);CHECK(fake::moves.size()==wheels);++scenarios;
            // The generation is forwarded into enqueue, closing check/use races.
            clear();press(70,120);fake::changeOnSend=true;move(90,120);release(90,120);CHECK(fake::moves.empty() && fake::clicks.empty());++scenarios;
            // Right-click also belongs to the connection on which it began.
            clear();press(85,20);++fake::link;release(85,20);CHECK(fake::clicks.empty());tap(85,20);CHECK(fake::clicks.size()==1 && fake::clicks[0]==2);++scenarios;
            // Event loss cancels once and release cannot turn into a tap.
            clear();press(70,120);auto n=fake::cancelMouse;lv_obj_send_event(pad(),LV_EVENT_PRESS_LOST,nullptr);CHECK(fake::cancelMouse==n+1);
            release(70,120);CHECK(fake::clicks.empty());++scenarios;
            // Hidden keyboard mode cannot run a delayed mouse press.
            tap(35,20);CHECK(lv_obj_is_hidden(pad()));n=fake::cancelMouse;lv_obj_send_event(pad(),LV_EVENT_PRESSED,nullptr);CHECK(fake::cancelMouse>n);CHECK(fake::moves.empty());
            tap(40,65);CHECK(fake::keys.size()==1 && fake::keys.back().code==0x06 && fake::keys.back().mods==apps::keys::Ctrl);
            tap(154,20);tap(40,65);CHECK(fake::keys.size()==2 && fake::keys.back().mods==apps::keys::Gui);++scenarios;
            // Multi-tap timer keeps its existing 700ms delay despite 4Hz rail snapshots.
            tap(210,20);clear();tap(120,95);advance(400);CHECK(fake::keys.empty());advance(350);CHECK(fake::keys.size()==1 && fake::keys[0].code==0x04);++scenarios;
            tap(120,95);CHECK(fake::app->onBack());advance(800);CHECK(fake::keys.size()==1);CHECK(!fake::app->onBack());++scenarios;
            // Radio failure cancels the in-flight gesture; no stale click follows.
            clear();press(70,120);fake::error=ESP_ERR_NO_MEM;n=fake::cancelMouse;move(90,120);CHECK(fake::cancelMouse>n);fake::error=ESP_OK;release(90,120);CHECK(fake::moves.empty() && fake::clicks.empty());++scenarios;
            clear();press(70,120);move(80,120);n=fake::cancelMouse;fake::mouseError=ESP_FAIL;advance(60);CHECK(fake::cancelMouse>n);release(80,120);CHECK(fake::clicks.empty());++scenarios;
            // Screen-off stops a manual gesture but retains intentional auto-click.
            clear();tap(170,20);CHECK(fake::app->powerHints()==(sys::power::StayLit|sys::power::KeepSession));
            press(80,130);fake::screen=false;n=fake::cancelMouse;sys::EventBus::publish({sys::EventType::ScreenOff});CHECK(fake::cancelMouse>n);
            release(80,130);CHECK(fake::clicks.empty());advance(1100);CHECK(fake::clicks.size()==1);fake::screen=true;
            tap(35,20);const auto autoCount=fake::clicks.size();advance(1100);CHECK(fake::clicks.size()==autoCount);CHECK(fake::app->onBack());++scenarios;
            // Full radio snapshots are no longer copied on every 50ms tick.
            const auto snapshots=fake::snapshots;for(int i=0;i<20;++i)advance(50);CHECK(fake::snapshots-snapshots<=5);
            // Drawing/layout events must not query/cancel the mouse channel.
            const auto reads=fake::linkReads;const auto cancelCount=fake::cancelMouse;
            for(int i=0;i<10;++i){lv_obj_invalidate(pad());lv_refr_now(display);}CHECK(fake::linkReads==reads && fake::cancelMouse==cancelCount);++scenarios;
            close();CHECK(timers()==baselineTimers);if(!cycle)baselineMemory=memory();else CHECK(memory()>=baselineMemory);
        }
    }
    CHECK(fake::subscriptions.empty() && timers()==baselineTimers);
    std::printf("BLE mouse UI: %u scenarios, %u checks; production App, real LVGL pointer events, 5 themes. Radio/device latency not exercised.\n",scenarios,checks);
}
