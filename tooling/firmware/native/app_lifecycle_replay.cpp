// Exercise production LVGL ownership. Hardware and the App workload are fakes.
#include "lvgl.h"
#include "ui/compact_ui.hpp"
#include "ui/control_states.h"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/battery_mgr.hpp"
#include "core/connectivity.hpp"
#include "core/system_service.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <new>
#define ESP_LOGW(...)

static unsigned checks=0,living=0,created=0,hidden=0,destroyed=0,focusResets=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace fake {bool screen=true,keyboard=false;}
// Windows host equivalent; firmware uses the SDK's thread-safe localtime_r.
static tm *localtime_r(const time_t *value,tm *out){*out=*std::localtime(value);return out;}
namespace sys {
class Power {public:
    static bool isScreenOn(){return fake::screen;}
    static bool sleepPending(){return false;}
    static bool takeThemeRestartError(){return false;}
    static bool takeShutdownError(){return false;}
    static void activity(){}
};
class Buttons {public:static bool blockHomeShortcut(){return false;}};
class BleKeyboardClient {public:static bool iineKeyboard(){return false;}};
bool BleHid::keyboardConnected(){return fake::keyboard;}
WifiSnapshot Connectivity::snapshot(){return {};}
BluetoothSnapshot SystemService::bluetooth(){return {};}
BatterySnapshot Battery::snapshot(){return {};}
uint32_t EventBus::subscribe(EventType,EventHandler){static uint32_t id=0;return ++id;}
}
namespace ui {
class HomeScreen {public:static void build(lv_obj_t *p){compact::label(p,"Home");}static void poll(){}static bool arranging(){return false;}static bool cancelArrange(){return false;}};
class Gesture {public:static void hook(lv_obj_t *){}};
class ExternalKeyboard {public:static void resetFocus(){++focusResets;}};
class QuickPanel {public:
    static void close(){} static bool back(){return false;}
    static bool visible(){return false;} static lv_obj_t *root(){return nullptr;}
};
}
#include "compact_ui.inc"
#include "text_input.inc"
#include "window_mgr.inc"

static lv_indev_t *touch=nullptr;
static int px=0,py=0;
static bool pressed=false;
static uint8_t buffer[240*240*2];
static void settle(){lv_tick_inc(40);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void sample(bool down,int x,int y){pressed=down;px=x;py=y;lv_tick_inc(20);lv_indev_read(touch);settle();}
static void tap(int x,int y){sample(true,x,y);sample(false,x,y);}
static size_t freeBytes(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void inputGone(){
    CHECK(!ui::TextInput::visible());CHECK(ui::bar==nullptr);CHECK(ui::target==nullptr);
    CHECK(ui::viewport==nullptr);CHECK(lv_obj_get_child_count(lv_layer_top())==0);
}

class Probe:public ui::App {
public:
    Probe(){CHECK(living==0);++living;++created;}
    ~Probe()override{CHECK(root()==nullptr);CHECK(!timer);--living;++destroyed;}
    const char *name()const override{return "Probe";}
    lv_obj_t *area=nullptr,*field=nullptr,*other=nullptr;
    lv_timer_t *timer=nullptr;
    void onCreate(lv_obj_t *parent)override{
        area=ui::compact::panel(parent,240,203,true);lv_obj_set_pos(area,0,37);
        field=ui::compact::networkField(area,"Name","",64);lv_obj_set_pos(field,0,0);
        other=ui::compact::networkField(area,"Password","",64);lv_obj_set_pos(other,0,42);
        timer=lv_timer_create([](lv_timer_t *){},100,this);
    }
    void onHide()override{++hidden;CHECK(!ui::TextInput::visible());lv_timer_pause(timer);}
    void onDestroy()override{lv_timer_delete(timer);timer=nullptr;}
};
static Probe *current=nullptr;
class FailingProbe:public ui::App {
public:
    static inline unsigned destroyed=0;
    const char *name()const override{return "Fail";}
    void onCreate(lv_obj_t *p)override{ui::compact::label(p,"partial");throw std::bad_alloc();}
    void onDestroy()override{++destroyed;}
};
static void openProbe(size_t index=0){
    auto &wm=ui::WindowManager::instance();const auto count=created;
    wm.openApp(index);CHECK(created==count);wm.processNavigation();
    CHECK(living==1);CHECK(current);settle();
}
static void edit(const char *expect){
    auto actions=lv_obj_get_child(ui::bar,1);
    lv_obj_send_event(lv_obj_get_child(actions,1),LV_EVENT_CLICKED,nullptr);
    CHECK(std::strcmp(lv_textarea_get_text(current->field),expect)==0);
}

int main(){
    lv_init();ui::theme::init(10);
    auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *d){d->point={px,py};d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
    ui::TextInput::init(touch);ui::TextInput::init(touch);inputGone();
    auto &wm=ui::WindowManager::instance();
    for(int i=0;i<2;++i)wm.registerApp("Probe",nullptr,[]{auto app=std::make_unique<Probe>();current=app.get();return app;});
    CHECK(living==0 && created==0);wm.init();settle();
    const unsigned baseTimers=timers();
    wm.registerApp("allocation failure",nullptr,[]()->std::unique_ptr<ui::App>{throw std::bad_alloc();});
    wm.registerApp("partial app",nullptr,[]{return std::make_unique<FailingProbe>();});
    wm.openApp(2);wm.processNavigation();CHECK(wm.atHome());
    wm.openApp(3);wm.processNavigation();CHECK(wm.atHome()&&FailingProbe::destroyed==1);
    CHECK(timers()==baseTimers);

    openProbe();tap(60,53);CHECK(ui::TextInput::visible());
    CHECK(lv_obj_get_height(current->area)==ui::TextInputLayout::viewport(203,37));
    lv_dropdown_set_selected(ui::type,1);lv_obj_send_event(ui::type,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_dropdown_set_selected(ui::character,0);edit(" ");
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(ui::bar,1),0),LV_EVENT_CLICKED,nullptr);
    CHECK(!*lv_textarea_get_text(current->field));
    lv_dropdown_set_selected(ui::type,2);lv_obj_send_event(ui::type,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_dropdown_set_selected(ui::character,25);edit("Z");
    ui::TextInput::hide();inputGone();settle();CHECK(lv_obj_get_height(current->area)==203);
    ui::TextInput::show(current->field);CHECK(lv_dropdown_get_selected(ui::type)==2);CHECK(lv_dropdown_get_selected(ui::character)==25);
    // A different field in the same viewport restores then reserves its height once.
    ui::TextInput::show(current->other);CHECK(ui::target==current->other);settle();
    CHECK(lv_obj_get_height(current->area)==ui::TextInputLayout::viewport(203,37));
    ui::TextInput::hide();settle();CHECK(lv_obj_get_height(current->area)==203);
    // Delete a field while its dropdown is open; no top-layer popup may survive.
    ui::TextInput::show(current->field);lv_dropdown_open(ui::character);
    CHECK(lv_dropdown_is_open(ui::character));lv_obj_delete(current->field);current->field=nullptr;inputGone();
    ui::TextInput::show(current->other);lv_dropdown_open(ui::type);
    lv_obj_delete(current->area);current->area=nullptr;inputGone();wm.goHome();settle();CHECK(timers()==baseTimers);
    CHECK(living==0 && hidden==destroyed);

    // Outside clicks dismiss; disabled fields, dark screen and hardware keyboard don't allocate UI.
    openProbe();ui::TextInput::show(current->field);tap(220,20);inputGone();
    lv_obj_add_state(current->field,LV_STATE_DISABLED);ui::TextInput::show(current->field);inputGone();
    lv_obj_remove_state(current->field,LV_STATE_DISABLED);fake::screen=false;ui::TextInput::show(current->field);inputGone();
    fake::screen=true;fake::keyboard=true;ui::TextInput::show(current->field);inputGone();
    fake::keyboard=false;ui::TextInput::show(current->field);fake::keyboard=true;ui::TextInput::show(current->field);inputGone();fake::keyboard=false;
    wm.goHome();settle();

    // Warm layout/font caches before observing repeated app teardown.
    for(int i=0;i<4;++i){openProbe();ui::TextInput::show(current->field);lv_dropdown_open(ui::type);settle();wm.goHome();settle();}
    const auto baseline=freeBytes();size_t minimum=baseline,maximumKeyboard=0;
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);
        for(int i=0;i<60;++i){
            openProbe(i%2);const auto before=freeBytes();ui::TextInput::show(current->field);settle();
            maximumKeyboard=std::max(maximumKeyboard,before-freeBytes());
            if(i%3==0){lv_dropdown_open(ui::type);settle();}
            if(i%3==1){lv_dropdown_open(ui::character);settle();}
            if(i%5==0){openProbe((i+1)%2);inputGone();ui::TextInput::show(current->field);}
            if(i%2){
                wm.goBack();inputGone();CHECK(!wm.atHome() && living==1);
                wm.goBack(); // The first Back dismisses input; the second exits.
            }else wm.goHome();
            settle();inputGone();
            CHECK(wm.atHome() && living==0);CHECK(hidden==destroyed);CHECK(created==destroyed);CHECK(timers()==baseTimers);
            minimum=std::min(minimum,freeBytes());
            // Allocator coalescing may recover a few extra bytes. It must never
            // lose free space after a completed App/keyboard teardown.
            if(freeBytes()<baseline)std::fprintf(stderr,"theme=%d cycle=%d baseline=%zu current=%zu\n",theme,i,baseline,freeBytes());
            CHECK(freeBytes()>=baseline);
        }
    }
    CHECK(focusResets>=created);
    std::printf("%u checks; 300 cycles across 5 themes; all probe Apps/timers/popups released; LVGL free baseline=%zu minimum=%zu; keyboard peak=%zu host bytes. No device measurement.\n",checks,baseline,minimum,maximumKeyboard);
    wm.goHome();lv_indev_delete(touch);lv_display_delete(display);lv_deinit();
}
