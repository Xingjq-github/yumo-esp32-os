#include "host_services.hpp"
#include "iot_button.h"
#include "apps/timer.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include "src/misc/lv_text_private.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

HostButton buttons[6];
static int64_t now=1000000;
static bool screen=true,sleepPending=false;
static unsigned screenshots=0,toggles=0,shutdowns=0,homeCalls=0,activity=0;
static int volume=50;
static std::vector<sys::Event> events;
static std::unique_ptr<apps::Timer> active;
int64_t esp_timer_get_time(){return now;}
int sys::Settings::volume(){return ::volume;}
void sys::SystemService::setVolume(int v){volume=v;}
void sys::Screenshot::request(){++screenshots;}
void sys::EventBus::publish(const Event &event){events.push_back(event);}
bool sys::Power::isScreenOn(){return screen;}
bool sys::Power::sleepPending(){return ::sleepPending;}
bool sys::Power::wokeFromSleep(){return false;}
bool sys::Power::slowIdle(){return false;}
void sys::Power::activity(){++::activity;}
void sys::Power::screenOn(){screen=true;}
void sys::Power::toggleScreen(){screen=!screen;++toggles;}
void sys::Power::powerOff(){++shutdowns;}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){
    ++homeCalls;
    if(active){auto root=active->root();active->onHide();assert(active->powerHints()==sys::power::None);
        active->onDestroy();lv_obj_delete(root);active.reset();}
}
void TextInput::bindField(lv_obj_t *){assert(false);}
}
static unsigned char pixels[240*240*2],buffer[240*240*2];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    for(int y=a->y1;y<=a->y2;++y)std::memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);
    lv_display_flush_ready(d);
}
static void capture(const char *name,int theme){
    char path[80];std::snprintf(path,sizeof(path),"%s-%d.ppm",name,theme);
    lv_refr_now(nullptr);FILE *f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n240 240\n255\n");
    for(int i=0;i<240*240;++i){unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={uint8_t(((p>>11)*255)/31),uint8_t((((p>>5)&63)*255)/63),uint8_t(((p&31)*255)/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
static void advance(uint64_t us=20000){now+=us;lv_tick_inc(uint32_t(us/1000));lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static lv_obj_t *label(lv_obj_t *root,const char *value){
    if(lv_obj_is_hidden(root))return nullptr;
    if(lv_obj_check_type(root,&lv_label_class) && !std::strcmp(lv_label_get_text(root),value))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto found=label(lv_obj_get_child(root,i),value))return found;
    return nullptr;
}
static lv_obj_t *find(const char *value){auto obj=label(lv_screen_active(),value);if(!obj)std::cerr<<"Missing label: "<<value<<std::endl;assert(obj);return obj;}
static void click(const char *value){auto button=lv_obj_get_parent(find(value));assert(!lv_obj_has_state(button,LV_STATE_DISABLED));lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);lv_obj_update_layout(lv_screen_active());}
static void emit(int pin,button_event_t event){auto &b=buttons[pin];b.event=event;assert(b.cb[event]);b.cb[event](&b,nullptr);}
static int64_t release(int pin){now+=10000;emit(pin,BUTTON_PRESS_DOWN);now+=40000;emit(pin,BUTTON_PRESS_UP);return now;}
static int64_t tap(int pin){auto at=release(pin);emit(pin,BUTTON_SINGLE_CLICK);emit(pin,BUTTON_PRESS_END);return at;}
static void doubleTap(int pin){release(pin);release(pin);emit(pin,BUTTON_DOUBLE_CLICK);emit(pin,BUTTON_PRESS_END);}
static void expectTime(uint64_t us){char a[40],b[8];core::stopwatch::format(a,sizeof(a),us,false);std::snprintf(b,sizeof(b),".%02u",unsigned(us/10000%100));find(a);find(b);}
static void open(){
    assert(!active);auto root=ui::compact::panel(lv_screen_active(),240,240);
    active=std::make_unique<apps::Timer>();active->setRoot(root);active->onCreate(root);active->onShow();advance();
    assert(active->powerHints()==(sys::power::StayLit|sys::power::KeepSession));assert(!active->allowsQuickPanel());
}
static unsigned timers(){unsigned count=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++count;return count;}
static lv_obj_t *lapPanel(){return lv_obj_get_child(active->root(),4);}
static lv_obj_t *live(){return lv_obj_get_child(lapPanel(),0);}
static lv_obj_t *history(){return lv_obj_get_child(lapPanel(),1);}
static void rowValues(lv_obj_t *row,unsigned number,uint64_t split,uint64_t total){
    char value[40];std::snprintf(value,sizeof(value),"%02u",number);
    assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(row,0)),value));
    core::stopwatch::format(value,sizeof(value),split);assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(row,1)),value));
    core::stopwatch::format(value,sizeof(value),total);assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(row,2)),value));
}
static void rowColor(lv_obj_t *row,uint32_t color){
    for(unsigned i=0;i<3;++i)assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(row,i),LV_PART_MAIN),lv_color_hex(color)));
}
static void checkLabels(lv_obj_t *obj){
    if(lv_obj_check_type(obj,&lv_label_class)){
        const auto value=lv_label_get_text(obj);auto font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);uint32_t i=0;
        while(value[i]){const auto cp=lv_text_encoded_next(value,&i);lv_font_glyph_dsc_t glyph{};
            if(cp=='\n')continue;
            if(!lv_font_get_glyph_dsc(font,&glyph,cp,0) || glyph.is_placeholder){std::cerr<<"Missing glyph "<<cp<<" in "<<value<<std::endl;assert(false);}}
        lv_point_t size;lv_text_get_size(&size,value,font,lv_obj_get_style_text_letter_space(obj,LV_PART_MAIN),lv_obj_get_style_text_line_space(obj,LV_PART_MAIN),10000,LV_TEXT_FLAG_NONE);
        if(size.x>lv_obj_get_width(obj)){std::cerr<<"Truncated label "<<value<<" needs "<<size.x<<" has "<<lv_obj_get_width(obj)<<std::endl;assert(false);}
        auto parent=lv_obj_get_parent(obj);lv_area_t a,b;lv_obj_get_coords(obj,&a);lv_obj_get_coords(parent,&b);
        if(a.x1<b.x1 || a.x2>b.x2){std::cerr<<"Horizontal overflow "<<value<<std::endl;assert(false);}
        if(a.y1<b.y1 || a.y2>b.y2){std::cerr<<"Vertical overflow "<<value<<std::endl;assert(false);}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)checkLabels(lv_obj_get_child(obj,i));
}
static void layout(){
    lv_obj_update_layout(lv_screen_active());checkLabels(active->root());
    lv_area_t back,split,time,pwr,key;
    lv_obj_get_coords(lv_obj_get_parent(find("返回")),&back);
    assert(back.x1==4 && back.x2==79 && back.y1==4 && back.y2==43);
    auto p=lv_obj_get_child(active->root(),0),k=lv_obj_get_child(active->root(),1);
    lv_obj_get_coords(p,&pwr);lv_obj_get_coords(k,&key);
    assert(pwr.x1==82 && pwr.x2==157 && key.x1==160 && key.x2==235 && pwr.y1==4 && key.y1==4);
    assert(lv_obj_get_height(p)==40 && lv_obj_get_height(k)==40);
    unsigned buttons=0;
    for(unsigned i=0;i<lv_obj_get_child_count(active->root());++i){auto o=lv_obj_get_child(active->root(),i);if(lv_obj_check_type(o,&lv_button_class)){++buttons;assert(lv_obj_get_y(o)==4);}}
    assert(buttons==3);
    auto clock=lv_obj_get_child(active->root(),3);lv_area_t a,b;
    lv_obj_get_coords(clock,&a);lv_obj_get_coords(lapPanel(),&b);
    assert(a.x1==4 && a.x2==235 && a.y1==50 && a.y2==125);
    assert(b.x1==4 && b.x2==235 && b.y1==132 && b.y2==235);
    auto group=lv_obj_get_child(clock,0);lv_area_t scaled;
    lv_obj_get_coords(group,&scaled);lv_obj_get_transformed_area(group,&scaled,LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
    assert(scaled.x1>=a.x1+3 && scaled.x2<=a.x2-2 && scaled.y1>=a.y1+3 && scaled.y2<=a.y2-2);
    assert(scaled.x2-scaled.x1>=220); // Fill the frame even at 100 hours.
    lv_obj_get_coords(live(),&a);lv_obj_get_coords(history(),&b);
    assert(a.y1==132 && a.y2==159 && b.y1==163 && b.y2==235);
    rowColor(live(),0xFFFFFF);
    assert(!label(lapPanel(),"记录") && !label(lapPanel(),"分段") && !label(lapPanel(),"累计"));
    for(unsigned i=0;i<lv_obj_get_child_count(active->root());++i){auto obj=lv_obj_get_child(active->root(),i);lv_area_t a;lv_obj_get_coords(obj,&a);assert(a.x1>=0 && a.x2<240 && a.y1>=0 && a.y2<240);}
}
static void splitDisplay(){
    ui::theme::init(4);open();rowValues(live(),1,0,0);assert(lv_obj_get_child_count(history())==0);
    click("开始");advance(3000000);rowValues(live(),1,3000000,3000000);
    click("记录");rowValues(live(),2,0,3000000);rowValues(lv_obj_get_child(history(),0),1,3000000,3000000);
    rowColor(lv_obj_get_child(history(),0),0xAAB5C0);
    advance(1000000);click("记录");rowValues(live(),3,0,4000000);
    rowColor(lv_obj_get_child(history(),0),0x56D89B);rowColor(lv_obj_get_child(history(),1),0xFF6B78);
    advance(2000000);click("记录");rowValues(live(),4,0,6000000);
    rowColor(lv_obj_get_child(history(),0),0xAAB5C0);rowColor(lv_obj_get_child(history(),1),0x56D89B);rowColor(lv_obj_get_child(history(),2),0xFF6B78);
    advance(1200000);rowValues(live(),4,1200000,7200000);rowColor(live(),0xFFFFFF);layout();capture("zones-live",4);
    click("暂停");advance(8000000);rowValues(live(),4,1200000,7200000);capture("zones-paused",4);
    click("继续");advance(800000);click("记录");rowValues(live(),5,0,8000000);
    // Ties at either end all receive that color; an all-equal set stays neutral.
    advance(1000000);click("记录");rowColor(lv_obj_get_child(history(),0),0x56D89B);rowColor(lv_obj_get_child(history(),3),0x56D89B);
    advance(3000000);click("记录");rowColor(lv_obj_get_child(history(),0),0xFF6B78);rowColor(lv_obj_get_child(history(),5),0xFF6B78);
    lv_area_t pinned;lv_obj_get_coords(live(),&pinned);lv_obj_scroll_to_y(history(),10000,LV_ANIM_OFF);advance(300000);
    assert(lv_obj_get_scroll_y(history())>0);rowValues(live(),7,300000,12300000);rowColor(live(),0xFFFFFF);
    lv_area_t after;lv_obj_get_coords(live(),&after);assert(!std::memcmp(&pinned,&after,sizeof(after)));capture("zones-scroll",4);
    click("暂停");click("复位");rowValues(live(),1,0,0);assert(lv_obj_get_child_count(history())==0 && lv_obj_get_scroll_y(history())==0);
    click("开始");advance(1000000);click("记录");advance(1000000);click("记录");
    rowColor(lv_obj_get_child(history(),0),0xAAB5C0);rowColor(lv_obj_get_child(history(),1),0xAAB5C0);
    // Compare only the retained 99 laps; evicting an old extreme must recolor the set.
    advance(5000000);click("记录");
    for(unsigned i=0;i<99;++i){advance(2000000);click("记录");}
    assert(lv_obj_get_child_count(history())==99);
    for(unsigned i=0;i<99;++i)rowColor(lv_obj_get_child(history(),i),0xAAB5C0);
    rowColor(live(),0xFFFFFF);layout();click("返回");
}
static void modelChecks(){
    using core::stopwatch::Model;Model m;
    m.start(1000000);m.record(2234567);assert(m.newest(0).splitUs==1234567);
    m.pause(3000000);assert(m.elapsed(9000000)==2000000 && !m.running());m.record(4000000);assert(m.size()==1);
    m.start(9000000);assert(m.elapsed(10000000)==3000000);m.record(10000000);assert(m.newest(0).splitUs==1765433);
    m.record(9500000);assert(m.newest(0).splitUs==0); // delayed input cannot underflow
    m.reset();m.start(20000000);
    for(unsigned i=1;i<=200;++i)m.record(20000000+i*1234567LL);
    assert(m.size()==99 && m.newest(0).number==200 && m.newest(98).number==102 && m.newest(0).splitUs==1234567);
    m.reset();m.start(1000000000LL);auto future=1000000000LL+100LL*3600*1000000+1230000;
    assert(m.elapsed(future)==100ULL*3600*1000000+1230000);char b[40];core::stopwatch::format(b,sizeof(b),m.elapsed(future));assert(!std::strcmp(b,"100:00:01.23"));
    // StayLit prevents timeout for running AND paused scenes. Dropping hints starts a fresh wait.
    sys::power::Policy p;sys::power::Config config;config.screenSeconds=10;
    sys::power::Input in;in.hints=sys::power::StayLit|sys::power::KeepSession;
    p.step(config,in);in.now=16*3600*1000;auto d=p.step(config,in);assert(!d.black && !d.deep && d.stage==sys::power::Stage::Active);
    in.hints=0;assert(!p.step(config,in).black);in.now+=9999;assert(!p.step(config,in).black);++in.now;assert(p.step(config,in).black);
    assert(config.screenSeconds==10); // No Settings/NVS mutation is used.
    sys::TimerKeyState keys;sys::TimerKey input;auto token=keys.claim();
    for(unsigned i=0;i<40;++i){keys.route(sys::TimerKeyState::Start,sys::TimerKeyState::Down,i*100);keys.route(sys::TimerKeyState::Start,sys::TimerKeyState::Up,i*100+50);keys.route(sys::TimerKeyState::Start,sys::TimerKeyState::End,i*100+60);}
    unsigned n=0;while(keys.take(token,input)){if(n<32)assert(input.action==sys::TimerKey::Key);else assert(input.action==sys::TimerKey::Overflow);++n;}assert(n==33);
    keys.route(sys::TimerKeyState::Boot,sys::TimerKeyState::Down,9000);keys.release(token);auto newToken=keys.claim();
    keys.route(sys::TimerKeyState::Boot,sys::TimerKeyState::Double,10000);assert(!keys.take(newToken,input));keys.release(token);
    keys.route(sys::TimerKeyState::Start,sys::TimerKeyState::Down,11000);keys.route(sys::TimerKeyState::Start,sys::TimerKeyState::Up,12000);assert(keys.take(newToken,input));
}
static void normalRouting(){
    auto v=volume;tap(4);assert(volume==v+5);tap(0);assert(volume==v);
    const auto s=screenshots;doubleTap(4);assert(screenshots==s+1);
    events.clear();doubleTap(0);assert(events.size()==1 && events[0].type==sys::EventType::HomeRequested);
    const auto t=toggles;tap(5);assert(!screen && toggles==t+1);tap(5);assert(screen);
    auto session=sys::Buttons::claimPageKeys();events.clear();tap(4);tap(0);
    assert(events.size()==2 && events[0].type==sys::EventType::NextPage && events[1].type==sys::EventType::PreviousPage);
    sys::Buttons::releasePageKeys(session);
    session=sys::Buttons::claimViewExit();tap(5);assert(sys::Buttons::takeViewExit(session) && screen);sys::Buttons::releaseViewExit(session);
    session=sys::Buttons::claimGameKeys();sys::Buttons::gameMode(session,true);sys::Buttons::gameRunning(session,true);
    emit(4,BUTTON_PRESS_DOWN);assert(sys::Buttons::takeGameKeys(session)==1);emit(4,BUTTON_PRESS_UP);emit(4,BUTTON_SINGLE_CLICK);emit(4,BUTTON_PRESS_END);
    events.clear();doubleTap(0);assert(events.empty());sys::Buttons::releaseGameKeys(session);
    sys::Buttons::setSystemPanel(true);events.clear();doubleTap(0);assert(events.size()==1 && events[0].type==sys::EventType::BackRequested);sys::Buttons::setSystemPanel(false);
}
int main(){
    modelChecks();lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
    sys::Buttons::init();normalRouting();splitDisplay();
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);const auto oldVolume=volume;const auto oldShots=screenshots,oldToggles=toggles;
        open();layout();expectTime(0);capture("ready",theme);
        assert(lv_obj_has_state(lv_obj_get_parent(find("复位")),LV_STATE_DISABLED));
        tap(0);advance();expectTime(0);assert(active && volume==oldVolume);
        const auto start=tap(4);advance(1234567);expectTime(now-start);find("记录");find("暂停");
        // Two quick KEY releases are two records; the driver's double callback must not screenshot.
        doubleTap(4);advance();find("01");find("02");assert(screenshots==oldShots);layout();capture("running",theme);
        const auto pause=tap(5);advance(4000000);expectTime(pause-start);find("继续");
        assert(screen && toggles==oldToggles);layout();capture("paused",theme);
        click("继续");const auto resumed=now;advance(1230000);expectTime(pause-start+now-resumed);
        click("记录");find("03");click("暂停");const auto elapsed=pause-start+now-resumed;
        advance(2000000);expectTime(elapsed);click("复位");expectTime(0);rowValues(live(),1,0,0);assert(lv_obj_get_child_count(history())==0);
        click("开始");advance(3661120000ULL);expectTime(3661120000ULL);layout();capture("hour",theme);
        click("记录");layout();
        for(unsigned i=0;i<105;++i){advance(20000);click("记录");}
        find("106");assert(!label(history(),"01"));layout();capture("many-records",theme);
        auto list=lv_obj_get_parent(lv_obj_get_parent(find("106")));assert(lv_obj_get_child_count(list)==99);
        lv_obj_scroll_to_y(list,100000,LV_ANIM_OFF);advance();assert(lv_obj_get_scroll_y(list)>0);capture("scrolled",theme);
        click("暂停");click("复位");find("开始");rowValues(live(),1,0,0);
        click("开始");advance(100ULL*3600*1000000);expectTime(100ULL*3600*1000000);layout();capture("hundred-hours",theme);
        // Long PWR must still reach the emergency handler without an extra pause/reset release.
        const auto shut=shutdowns;emit(5,BUTTON_PRESS_DOWN);emit(5,BUTTON_LONG_PRESS_START);emit(5,BUTTON_PRESS_UP);emit(5,BUTTON_PRESS_END);assert(shutdowns==shut+1);
        advance();find("暂停");
        const auto home=homeCalls;doubleTap(0);advance();assert(!active && homeCalls==home+1);advance(500000);
        assert(lv_obj_get_child_count(lv_screen_active())==0 && volume==oldVolume && screenshots==oldShots && toggles==oldToggles);
        // Holding a key while leaving and reopening cannot deliver the old release into the new App.
        open();emit(4,BUTTON_PRESS_DOWN);click("返回");assert(!active);open();emit(4,BUTTON_PRESS_UP);emit(4,BUTTON_SINGLE_CLICK);emit(4,BUTTON_PRESS_END);advance();expectTime(0);find("开始");
        // A press begun outside the App cannot start it, either.
        click("返回");emit(4,BUTTON_PRESS_DOWN);open();emit(4,BUTTON_PRESS_UP);emit(4,BUTTON_SINGLE_CLICK);emit(4,BUTTON_PRESS_END);advance();expectTime(0);
        tap(4);advance();find("暂停");click("返回");advance(1000000);assert(!active);
        normalRouting();
    }
    // Exit stays reachable even if a stalled GUI has filled the physical-input queue.
    open();for(unsigned i=0;i<40;++i)tap(4);doubleTap(0);advance();assert(!active);
    const auto baselineTimers=timers();
    for(unsigned i=0;i<50;++i){open();assert(timers()==baselineTimers+1);tap(4);advance();click("返回");advance();assert(lv_obj_get_child_count(lv_screen_active())==0 && timers()==baselineTimers);}
    lv_display_delete(display);lv_deinit();
    std::cout<<"PASS: three-zone layout, pinned white live split/total, lap capture/reset, pause/resume, fastest/slowest colors and ties/eviction, stopwatch arithmetic, 99-record bound, overflow/session fencing, foreground power policy, production physical callbacks and touch controls, five LVGL themes, BOOT double exit, long-PWR escape, existing page/game/panel/screenshot routing, 50 repeated lifecycles.\n";
}
