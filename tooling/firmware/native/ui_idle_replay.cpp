#include "lvgl.h"
#include "apps/rps_match.hpp"
#include "core/theme_model.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
static unsigned checks=0,flushes=0,idleFlushes=0,settlements=0;
static uint64_t pixelDigest=1469598103934665603ULL,stateDigest=pixelDigest;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
using esp_err_t=int;
constexpr int ESP_OK=0;
namespace fake {static bool lit=true,usbBusy=false,usbOn=false,usbIdle=true,storage=true,hotspot=false,motion=false,busy[3]{};static int hour=12,minute=34,bright=35,volume=50;}
static tm *localtime_r(const time_t *,tm *out){*out={};out->tm_hour=fake::hour;out->tm_min=fake::minute;return out;}
namespace sys {
enum class WifiState {Off,Connected,Searching,Connecting,Failed};
enum class BleHidState {Off,Connected,Starting,Connecting};
enum class ServiceDomain {Wifi,Bluetooth,System};
enum class ServiceAction {None,DeepSleep,WifiEnable,BluetoothEnable,Motion};
struct Wifi {bool enabled=false,transitioning=false;WifiState state=WifiState::Off;};
struct Bluetooth {bool enabled=false,error=false;BleHidState state=BleHidState::Off;struct {bool connected=false;}peer;bool hasError()const{return error;}};
struct BatteryState {int percent=80;bool valid=true,chargingKnown=false,charging=false;};
struct Result {ServiceAction action=ServiceAction::None;int error=0;unsigned revision=0;};
static Wifi wifi;static Bluetooth bluetooth;static BatteryState battery;
class Power {public:static bool isScreenOn(){return fake::lit;}};
class Connectivity {public:static Wifi snapshot(){return wifi;}static bool hotspotReserved(){return fake::hotspot;}};
class SystemService {public:static Bluetooth bluetooth(){return sys::bluetooth;}static bool busy(ServiceDomain d){return fake::busy[unsigned(d)];}static Result result(ServiceDomain){return {};}};
class Battery {public:static BatteryState snapshot(){return battery;}};
class Settings {public:static int brightness(){return fake::bright;}static int volume(){return fake::volume;}static bool motionSensorOn(){return fake::motion;}};
class UsbStorage {public:static bool transitioning(){return fake::usbBusy;}static bool enabled(){return fake::usbOn;}static bool idle(){return fake::usbIdle;}static const char *statusText(){return "USB";}};
}
namespace ui {
namespace theme {
static sys::themes::Id id=sys::themes::Id::Terminal;
static auto palette(){return sys::themes::palette(id);}
static lv_color_t bg(){return lv_color_hex(palette().background);}static lv_color_t fg(){return lv_color_hex(palette().foreground);}
static lv_color_t danger(){return lv_color_hex(palette().danger);}static lv_color_t warning(){return lv_color_hex(palette().warning);}
static lv_color_t success(){return lv_color_hex(palette().success);}static lv_color_t muted(){return lv_color_hex(palette().muted);}
static lv_color_t edge(){return lv_color_hex(palette().edge);}static bool light(){return sys::themes::light(id);}
}
namespace compact {
static void setText(lv_obj_t *o,const char *s){if(o&&strcmp(lv_label_get_text(o),s))lv_label_set_text(o,s);}
static void checked(lv_obj_t *o,bool v){if(!o)return;if(v)lv_obj_add_state(o,LV_STATE_CHECKED);else lv_obj_remove_state(o,LV_STATE_CHECKED);}
static void disabled(lv_obj_t *o,bool v){if(!o)return;if(v)lv_obj_add_state(o,LV_STATE_DISABLED);else lv_obj_remove_state(o,LV_STATE_DISABLED);}
}
#include "status.inc"
}
namespace panel {
namespace c=ui::compact;namespace theme=ui::theme;
static lv_obj_t *brightness,*volume,*values[2],*tiles[3],*usb,*usbText;
static bool usbWasBusy=false,deepWaiting=false;static unsigned resultRevision[3]{};
static bool storageIdle(){return fake::storage;}static void notice(const char *){}static void prompt(bool){}static void report(int){}
#include "panel.inc"
}
namespace apps {
namespace c=ui::compact;
static lv_color_t teamInk(unsigned side){const bool light=ui::theme::light();return lv_color_hex(side?(light?0xA92733:0xF1707A):(light?0x18558D:0x69B4FF));}
struct Randomizer {
    rps::Match m_match;
    unsigned m_pkStep[2]{},m_pkTotal[2]{};uint32_t m_pkDue[2]{};int m_pkCursor[2]{-1,-1};
    lv_obj_t *m_pkScore[2]{},*m_pkCells[2][3]{},*m_pkResult=nullptr,*m_start=nullptr;
    bool finished=false;
    void finish(){finished=true;}
    void paintPk();void tickPk();
};
#include "random.inc"
}
static uint8_t pixels[240*240*2],bufferA[240*20*2],bufferB[sizeof(bufferA)];
static void hash(uint64_t &digest,const void *data,size_t size){for(size_t i=0;i<size;++i){digest^=static_cast<const uint8_t *>(data)[i];digest*=1099511628211ULL;}}
static void flush(lv_display_t *d,const lv_area_t *r,uint8_t *p){++flushes;for(int y=r->y1;y<=r->y2;++y)memcpy(pixels+(y*240+r->x1)*2,p+(y-r->y1)*(r->x2-r->x1+1)*2,(r->x2-r->x1+1)*2);lv_display_flush_ready(d);}
static void render(){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);}
static void capture(){render();hash(pixelDigest,pixels,sizeof(pixels));}
static lv_obj_t *label(lv_obj_t *p,const char *s,int x,int y){auto o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);return o;}
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h){auto o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,ui::theme::bg(),0);lv_obj_set_scrollable(o,false);return o;}
template<class F>static void idle(F function){render();const auto before=flushes;function();render();idleFlushes+=flushes-before;
#ifdef EXPECT_IDLE
CHECK(flushes==before);
#endif
}
static void statusTests(lv_obj_t *root){
    auto bar=box(root,0,0,240,24);ui::s_statusTime=label(bar,"",2,2);ui::s_statusWifi=label(bar,"",100,2);ui::s_statusBluetooth=label(bar,"",125,2);ui::s_statusBattery=label(bar,"",162,2);
    for(int theme:{4,5,6,8,10}){ui::theme::id=sys::themes::normalize(theme);
        for(int state=0;state<7;++state){
            sys::wifi={state!=0,state==2,state==4?sys::WifiState::Failed:state==3?sys::WifiState::Connected:state==2?sys::WifiState::Connecting:sys::WifiState::Off};
            sys::bluetooth.enabled=state!=0;sys::bluetooth.error=state==4;sys::bluetooth.state=state==3?sys::BleHidState::Connected:state==2?sys::BleHidState::Connecting:sys::BleHidState::Off;
            sys::battery.percent=state==5?5:80;sys::battery.valid=state!=6;sys::battery.chargingKnown=sys::battery.charging=state==3;
            fake::minute=34+state;ui::updateStatusBar(nullptr);capture();
            CHECK(strcmp(lv_label_get_text(ui::s_statusWifi),state?LV_SYMBOL_WIFI:"")==0);
            CHECK(strcmp(lv_label_get_text(ui::s_statusBluetooth),state?LV_SYMBOL_BLUETOOTH:"")==0);
            for(int i=0;i<8;++i)idle([]{ui::updateStatusBar(nullptr);});
        }
    }
    lv_obj_set_hidden(bar,true);render();fake::minute=59;idle([]{ui::updateStatusBar(nullptr);});lv_obj_delete(bar);
}
static void panelTests(lv_obj_t *root){
    auto screen=box(root,0,0,240,240);
    for(int i=0;i<2;++i){auto slider=lv_slider_create(screen);lv_obj_set_pos(slider,10,8+i*35);lv_obj_set_size(slider,140,16);lv_slider_set_range(slider,i?0:5,100);if(i)panel::volume=slider;else panel::brightness=slider;panel::values[i]=label(screen,"",165,8+i*35);}
    for(int i=0;i<3;++i){panel::tiles[i]=box(screen,6+i*78,96,72,50);lv_obj_set_style_border_width(panel::tiles[i],2,0);}
    panel::usb=box(screen,6,160,220,40);panel::usbText=label(panel::usb,"",4,4);
    for(int theme:{4,5,6,8,10}){ui::theme::id=sys::themes::normalize(theme);
        for(int state=0;state<7;++state){
            sys::wifi.enabled=state!=0;sys::wifi.transitioning=state==2;sys::wifi.state=state==4?sys::WifiState::Failed:state==3?sys::WifiState::Connected:sys::WifiState::Off;
            sys::bluetooth.enabled=state!=0;sys::bluetooth.error=state==4;sys::bluetooth.peer.connected=state==3;sys::bluetooth.state=state==2?sys::BleHidState::Starting:sys::BleHidState::Off;
            fake::busy[2]=state==2;fake::motion=state==3;fake::hotspot=state==5;fake::storage=state!=6;
            panel::refresh();capture();
            for(int i=0;i<8;++i)idle([]{panel::refresh();});
            // Default color comparison must not strip a pressed-state override.
            auto tile=panel::tiles[0];lv_obj_set_style_border_color(tile,lv_color_hex(0xAB1234),LV_STATE_PRESSED);lv_obj_add_state(tile,LV_STATE_PRESSED);render();
            idle([]{panel::refresh();});CHECK(lv_color_eq(lv_obj_get_style_border_color(tile,LV_PART_MAIN),lv_color_hex(0xAB1234)));
            lv_obj_remove_state(tile,LV_STATE_PRESSED);render();
        }
    }
    lv_obj_delete(screen);
}
static void randomTests(lv_obj_t *root){
    for(int stagger:{0,120,2500})for(unsigned a=13;a<=15;++a)for(unsigned b=13;b<=15;++b){
        auto screen=box(root,0,0,240,240);apps::Randomizer game;game.m_pkTotal[0]=a;game.m_pkTotal[1]=b;
        for(int side=0;side<2;++side){game.m_pkScore[side]=label(screen,"",side*140,0);for(int hand=0;hand<3;++hand){auto tile=box(screen,side*162,26+46*hand,78,44);label(tile,"X",10,10);game.m_pkCells[side][hand]=tile;}}
        game.m_pkResult=label(screen,"",90,70);game.m_start=box(screen,0,200,240,40);label(game.m_start,"",8,8);
        const auto started=lv_tick_get();CHECK(game.m_match.begin(0));game.m_pkDue[0]=started;
        bool second=false;game.paintPk();render();
        for(unsigned elapsed=0;elapsed<7000;elapsed+=20){
            if(!second&&elapsed>=unsigned(stagger)){CHECK(game.m_match.begin(1));game.m_pkDue[1]=lv_tick_get();second=true;}
            const bool changed=(game.m_match.state[0]==apps::rps::Match::State::Running&&int32_t(lv_tick_get()-game.m_pkDue[0])>=0)||(game.m_match.state[1]==apps::rps::Match::State::Running&&int32_t(lv_tick_get()-game.m_pkDue[1])>=0);
            if(changed)game.tickPk();else idle([&]{game.tickPk();});
            capture();hash(stateDigest,game.m_pkStep,sizeof(game.m_pkStep));hash(stateDigest,game.m_pkCursor,sizeof(game.m_pkCursor));hash(stateDigest,game.m_match.choice,sizeof(game.m_match.choice));hash(stateDigest,game.m_match.score,sizeof(game.m_match.score));hash(stateDigest,&game.m_match.result,sizeof(game.m_match.result));
            if(game.m_match.result>=0){CHECK(game.m_match.result==apps::rps::winner((a-1)%3,(b-1)%3));++settlements;break;}
            lv_tick_inc(20);
        }
        CHECK(game.m_match.result>=0);lv_obj_delete(screen);render();
    }
}
int main(){
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,bufferA,bufferB,sizeof(bufferA),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    auto root=lv_screen_active();lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_style_bg_color(root,lv_color_hex(0x102030),0);lv_obj_set_style_pad_all(root,0,0);lv_obj_set_scrollable(root,false);
    statusTests(root);panelTests(root);randomTests(root);lv_display_delete(display);lv_deinit();
    printf("{\"checks\":%u,\"idle_flushes\":%u,\"pixel_digest\":\"%llu\",\"state_digest\":\"%llu\",\"pk_settlements\":%u}\n",checks,idleFlushes,(unsigned long long)pixelDigest,(unsigned long long)stateDigest,settlements);
}
