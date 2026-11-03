#include "lvgl.h"
#include "ui/window_mgr.hpp"
#include "ui/vault_pattern.hpp"
#include "core/event_bus.hpp"
#include "core/theme_model.hpp"
#include "core/vault_format.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#define ESP_LOGW(...)
static unsigned checks=0,comparisons=0,living=0,destroyed=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static tm *localtime_r(const time_t *,tm *out){*out={};out->tm_hour=12;out->tm_min=34;return out;}
namespace sys {
class Power {public:static inline bool themeError=false;static bool isScreenOn(){return true;}static bool sleepPending(){return false;}static void activity(){}static bool takeThemeRestartError(){const bool e=themeError;themeError=false;return e;}static bool takeShutdownError(){return false;}};
class Buttons {public:static bool blockHomeShortcut(){return false;}};
enum class WifiState {Connected,Searching,Connecting,Failed};
enum class BleHidState {Connected,Starting,Connecting};
struct Wifi {bool enabled=false,transitioning=false;WifiState state=WifiState::Failed;};
struct Bluetooth {bool enabled=false;BleHidState state=BleHidState::Starting;bool hasError()const{return false;}};
struct BatteryState {int percent=80;bool valid=true,chargingKnown=false,charging=false;};
class Connectivity{public:static Wifi snapshot(){return {};}};
class SystemService{public:static Bluetooth bluetooth(){return {};}};
class Battery{public:static BatteryState snapshot(){return {};}};
uint32_t EventBus::subscribe(EventType,EventHandler){return 1;}
}
namespace ui {
namespace theme {
static sys::themes::Id id=sys::themes::Id::Terminal;
static auto palette(){return sys::themes::palette(id);}
static lv_color_t bg(){return lv_color_hex(palette().background);}static lv_color_t fg(){return lv_color_hex(palette().foreground);}
static lv_color_t accent(){return lv_color_hex(palette().accent);}static lv_color_t muted(){return lv_color_hex(palette().muted);}
static lv_color_t surface(){return lv_color_hex(palette().surface);}static lv_color_t danger(){return lv_color_hex(palette().danger);}
static lv_color_t warning(){return lv_color_hex(palette().warning);}static lv_color_t success(){return lv_color_hex(palette().success);}
constexpr int kStatusPad=4;
}
namespace compact {
static lv_obj_t *panel(lv_obj_t *p,int w,int h,bool=false){auto o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,theme::bg(),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_scrollable(o,false);return o;}
static lv_obj_t *label(lv_obj_t *p,const char *s,int w=LV_SIZE_CONTENT){auto o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_width(o,w);return o;}
static void setText(lv_obj_t *o,const char *s){lv_label_set_text(o,s);}
}
class HomeScreen {public:
    static void build(lv_obj_t *p){for(int y=0;y<216;y+=12)for(int x=0;x<240;x+=12){auto o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,12,12);lv_obj_set_style_bg_color(o,lv_color_hex(((x+y)/12)%2?0xFF00FF:0x00FF00),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);}auto t=compact::label(p,"HOME OLD FRAME",220);lv_obj_set_pos(t,10,184);}
    static void poll(){}static bool arranging(){return false;}static bool cancelArrange(){return false;}
};
class Gesture{public:static void hook(lv_obj_t *){}};
class ExternalKeyboard{public:static void resetFocus(){}};
class TextInput{public:static void hide(){}static bool visible(){return false;}};
class QuickPanel{public:static void close(){}static bool back(){return false;}static bool visible(){return false;}static lv_obj_t *root(){return nullptr;}};
}
#include "window_mgr.inc"
#include "vault_pattern.inc"
static uint8_t pixels[240*240*2],a[240*20*2],b[240*20*2];
static void flush(lv_display_t *d,const lv_area_t *r,uint8_t *data){
    const unsigned count=lv_area_get_size(r);lv_draw_sw_rgb565_swap(data,count);
    for(int y=r->y1;y<=r->y2;++y)for(int x=r->x1;x<=r->x2;++x){unsigned src=((y-r->y1)*(r->x2-r->x1+1)+x-r->x1)*2,dst=(y*240+x)*2;pixels[dst]=data[src+1];pixels[dst+1]=data[src];}
    lv_display_flush_ready(d);
}
static void render(){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);}
static void capture(const char *name){FILE *f=fopen(name,"wb");CHECK(f);fprintf(f,"P6\n240 240\n255\n");for(int i=0;i<240*240;++i){unsigned p=pixels[2*i]+256*pixels[2*i+1];unsigned char rgb[]={(unsigned char)((p>>11)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);}
class Probe:public ui::App {
public:explicit Probe(bool pattern):pattern(pattern){++living;}~Probe()override{CHECK(!root());--living;++destroyed;}const char *name()const override{return "Probe";}
    bool pattern;ui::VaultPattern view;
    void onCreate(lv_obj_t *p)override {if(pattern){auto body=ui::compact::panel(p,240,240);view.create(body,"",true,nullptr,nullptr);}else{auto t=ui::compact::label(p,"APP ONLY",160);lv_obj_set_pos(t,40,90);auto o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,15,180);lv_obj_set_size(o,210,40);lv_obj_set_style_bg_color(o,ui::theme::accent(),0);lv_obj_set_style_bg_opa(o,LV_OPA_30,0);}}
    void onDestroy()override{view.detach();}
};
class FailureProbe:public ui::App {
public:explicit FailureProbe(int phase):phase(phase){++living;}~FailureProbe()override{CHECK(!root());--living;++destroyed;}int phase;
    const char *name()const override{return "Failure";}
    void onCreate(lv_obj_t *p)override{ui::compact::label(p,"PARTIAL CREATE");if(phase==0)throw std::bad_alloc();}
    void onShow()override{if(phase==1)throw std::bad_alloc();}
    void onDestroy()override{if(phase==2)throw std::bad_alloc();}
};
static void independentReference(bool pattern,std::array<uint8_t,sizeof(pixels)> &out){
    auto previous=lv_screen_active();auto screen=lv_obj_create(nullptr);lv_obj_remove_style_all(screen);lv_obj_set_style_bg_color(screen,ui::theme::bg(),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_set_style_text_color(screen,ui::theme::fg(),0);lv_obj_set_scrollable(screen,false);
    Probe probe(pattern);probe.setRoot(screen);probe.onCreate(screen);lv_screen_load(screen);render();memcpy(out.data(),pixels,sizeof(pixels));probe.onDestroy();probe.setRoot(nullptr);lv_screen_load(previous);lv_obj_delete(screen);render();
}
static void compare(const std::array<uint8_t,sizeof(pixels)> &expected,int theme,bool pattern,unsigned cycle){
    unsigned diff=0,bottom=0;for(unsigned i=0;i<sizeof(pixels);i+=2)if(pixels[i]!=expected[i]||pixels[i+1]!=expected[i+1]){++diff;if(i/2/240>=216)++bottom;}
    if(diff){capture("failure.ppm");fprintf(stderr,"theme=%d pattern=%d cycle=%u: %u changed pixels, bottom24=%u\n",theme,pattern,cycle,diff,bottom);std::exit(1);}++comparisons;
}
int main(){
    lv_init();auto d=lv_display_create(240,240);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,a,b,sizeof(a),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    auto &wm=ui::WindowManager::instance();wm.init();wm.registerApp("Pattern",nullptr,[]{return std::make_unique<Probe>(true);});wm.registerApp("Plain",nullptr,[]{return std::make_unique<Probe>(false);});render();capture("home.ppm");
    auto homeScreen=lv_screen_active();
    std::array<uint8_t,sizeof(pixels)> home,expected;memcpy(home.data(),pixels,sizeof(pixels));
    for(int theme:{4,5,6,8,10}){ui::theme::id=sys::themes::normalize(theme);for(bool pattern:{true,false}){
        independentReference(pattern,expected);
        for(unsigned cycle=0;cycle<8;++cycle){wm.openApp(pattern?0:1);wm.processNavigation();render();CHECK(!wm.atHome());CHECK(living==1);CHECK(lv_obj_is_valid(lv_screen_active()));CHECK(lv_screen_active()==wm.screen());CHECK(lv_screen_active()!=homeScreen);CHECK(lv_obj_get_screen(wm.inputRoot())==wm.screen());compare(expected,theme,pattern,cycle);
            // Redraw bottom-edge and tiny disjoint regions using reused swapped buffers.
            lv_area_t bottom={0,216,239,239};lv_obj_invalidate_area(wm.inputRoot(),&bottom);render();compare(expected,theme,pattern,cycle);
            lv_area_t tiny={42,42,66,66};lv_obj_invalidate_area(wm.inputRoot(),&tiny);render();compare(expected,theme,pattern,cycle);
            if(cycle==0){char path[64];snprintf(path,sizeof(path),"theme-%d-%s.ppm",theme,pattern?"pattern":"plain");capture(path);}
            auto outgoing=wm.screen();if(cycle%2)wm.goBack();else wm.goHome();render();CHECK(wm.atHome());CHECK(living==0);CHECK(lv_screen_active()==homeScreen);CHECK(lv_obj_is_valid(homeScreen));CHECK(!lv_obj_is_valid(outgoing));compare(home,theme,false,cycle);
        }
    }}
    // Allocation, onCreate, onShow and cleanup exceptions must all leave a live
    // home screen and release partial app widgets/screens exactly once.
    wm.registerApp("Factory failure",nullptr,[]()->std::unique_ptr<ui::App>{throw std::bad_alloc();});
    for(int phase=0;phase<3;++phase)wm.registerApp("Failure",nullptr,[phase]{return std::make_unique<FailureProbe>(phase);});
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);const auto beforeFailures=memory.free_size;
    for(unsigned repeat=0;repeat<10;++repeat)for(unsigned index=2;index<6;++index){
        wm.openApp(index);wm.processNavigation();if(index==5){CHECK(!wm.atHome());wm.goHome();}render();
        CHECK(wm.atHome());CHECK(living==0);CHECK(lv_screen_active()==homeScreen);CHECK(wm.screen()==homeScreen);CHECK(lv_obj_is_valid(homeScreen));
        lv_mem_monitor(&memory);CHECK(memory.free_size>=beforeFailures);
    }
    // A top-layer system notice survives app-screen deletion until its timer.
    sys::Power::themeError=true;wm.processNavigation();CHECK(lv_obj_get_child_count(lv_layer_top())==1);
    auto toast=lv_obj_get_child(lv_layer_top(),0);wm.openApp(0);wm.processNavigation();render();CHECK(lv_obj_is_valid(toast));
    wm.goHome();render();CHECK(lv_obj_is_valid(toast));lv_tick_inc(4001);lv_timer_handler();render();CHECK(lv_obj_get_child_count(lv_layer_top())==0);CHECK(!lv_obj_is_valid(toast));CHECK(living==0);
    wm.goHome();lv_display_delete(d);lv_deinit();printf("{\"passed\":true,\"checks\":%u,\"pixel_equal_comparisons\":%u,\"themes\":5,\"navigation_cycles\":80,\"failure_cycles\":40,\"released_apps\":%u,\"buffer_rows\":20,\"dma_like_byte_swap\":true,\"system_notice_lifetime\":true}\n",checks,comparisons,destroyed);
}
