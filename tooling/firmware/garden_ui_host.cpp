#include "apps/garden.hpp"
#include "host_services.hpp"
#include "iot_button.h"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include "src/misc/lv_text_private.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
namespace g=core::garden;
static std::unique_ptr<apps::Garden> app;
static std::shared_ptr<g::Job> pending;
static g::store::Snapshot saved;
static int ioError=0;
static bool screen=true,busy=false;
static unsigned writes=0;
HostButton buttons[6];
static int volume=50;
static unsigned shots=0,shutdowns=0;
static std::vector<sys::Event> events;
int64_t esp_timer_get_time(){return int64_t(lv_tick_get())*1000;}
int sys::Settings::volume(){return ::volume;}
void sys::SystemService::setVolume(int v){volume=v;}
void sys::Screenshot::request(){++shots;}
void sys::EventBus::publish(const Event &event){events.push_back(event);}
bool sys::Power::sleepPending(){return false;}
bool sys::Power::wokeFromSleep(){return false;}
bool sys::Power::slowIdle(){return false;}
void sys::Power::screenOn(){screen=true;}
void sys::Power::toggleScreen(){screen=!screen;}
void sys::Power::powerOff(){++shutdowns;}
uint32_t esp_random(){static uint32_t s=78;return ++s;}
bool sys::Power::isScreenOn(){return screen;}
void sys::Power::activity(){}
namespace core::garden {
bool idle(){return !busy && !pending;}
std::shared_ptr<Job> open(){if(!idle())return {};pending=std::make_shared<Job>();return pending;}
std::shared_ptr<Job> save(const State &s,uint64_t revision,uint64_t generation,sd::Access access){
    if(!idle())return {};assert(generation==9 && access.epoch==3);pending=std::make_shared<Job>();
    pending->state=s;pending->expected=revision;pending->saving=true;return pending;
}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager wm;return wm;}
void WindowManager::goHome(){if(app){auto root=app->root();app->onHide();app->onDestroy();lv_obj_delete(root);app.reset();}}
void TextInput::bindField(lv_obj_t *){assert(false);}
}
static void complete(){
    if(!pending)return;auto j=pending;pending.reset();j->error=ioError;
    if(!j->error){if(j->saving){assert(!j->cancelled);assert(j->expected==saved.revision);saved={j->state,j->expected+1,true,false};++writes;}
        j->result=saved;j->generation=9;j->access={3,true};}
    j->done.store(true);
}
static unsigned char pixels[240*240*2],buffer[240*240*2];
static lv_indev_t *pointer=nullptr;
static lv_point_t point{120,110};
static bool pressed=false;
static void readPointer(lv_indev_t *,lv_indev_data_t *data){data->point=point;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static void tapPet(){point={120,110};pressed=true;lv_indev_read(pointer);pressed=false;lv_indev_read(pointer);}
static unsigned objects(lv_obj_t *obj){unsigned count=1;for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)count+=objects(lv_obj_get_child(obj,i));return count;}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    for(int y=a->y1;y<=a->y2;++y)std::memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);
    lv_display_flush_ready(d);
}
static void capture(const char *name,int theme){
    char path[80];std::snprintf(path,sizeof(path),"%s-%d.ppm",name,theme);lv_refr_now(nullptr);
    FILE *f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n240 240\n255\n");
    for(int i=0;i<240*240;++i){unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={uint8_t(((p>>11)*255)/31),uint8_t((((p>>5)&63)*255)/63),uint8_t(((p&31)*255)/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
static void advance(unsigned ms=100){lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void settle(){complete();advance();}
static lv_obj_t *label(lv_obj_t *root,const char *value){
    if(lv_obj_is_hidden(root))return nullptr;
    if(lv_obj_check_type(root,&lv_label_class) && !std::strcmp(lv_label_get_text(root),value))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto found=label(lv_obj_get_child(root,i),value))return found;return nullptr;
}
static lv_obj_t *find(const char *value){auto obj=label(lv_screen_active(),value);if(!obj)std::cerr<<"Missing label: "<<value<<std::endl;assert(obj);return obj;}
static void click(const char *value){auto b=lv_obj_get_parent(find(value));assert(!lv_obj_has_state(b,LV_STATE_DISABLED));lv_obj_send_event(b,LV_EVENT_CLICKED,nullptr);advance();}
static void open(){assert(!app);auto root=ui::compact::panel(lv_screen_active(),240,240);app=std::make_unique<apps::Garden>();app->setRoot(root);app->onCreate(root);app->onShow();settle();assert(app->powerHints()==sys::power::None);}
static void close(){ui::WindowManager::instance().goHome();advance();assert(lv_obj_get_child_count(lv_screen_active())==0);}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void layout(lv_obj_t *obj){
    if(lv_obj_check_type(obj,&lv_label_class)){
        auto value=lv_label_get_text(obj);auto font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);uint32_t index=0;
        while(value[index]){auto cp=lv_text_encoded_next(value,&index);if(cp=='\n')continue;lv_font_glyph_dsc_t glyph{};
            if(!lv_font_get_glyph_dsc(font,&glyph,cp,0) || glyph.is_placeholder){std::cerr<<"Missing glyph "<<cp<<" in "<<value<<std::endl;assert(false);}}
        auto mode=lv_label_get_long_mode(obj);
        if(mode!=LV_LABEL_LONG_MODE_WRAP && mode!=LV_LABEL_LONG_MODE_SCROLL_CIRCULAR){
            lv_point_t size;lv_text_get_size(&size,value,font,0,0,10000,LV_TEXT_FLAG_NONE);
            if(size.x>lv_obj_get_width(obj)){std::cerr<<"Clipped label: "<<value<<" width "<<size.x<<" available "<<lv_obj_get_width(obj)<<std::endl;assert(false);}}
        lv_area_t a,b;lv_obj_get_coords(obj,&a);lv_obj_get_coords(lv_obj_get_parent(obj),&b);
        if(a.x1<b.x1 || a.x2>b.x2 || a.y2>b.y2){std::cerr<<"Overflow: "<<value<<std::endl;assert(false);}
    }
    if(lv_obj_check_type(obj,&lv_button_class)){assert(lv_obj_get_width(obj)>=36 && lv_obj_get_height(obj)>=36);}
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)layout(lv_obj_get_child(obj,i));
}
static void check(){lv_obj_update_layout(app->root());layout(app->root());}
static void install(g::State state){assert(g::valid(state));saved={state,1,true,false};}
static void pressAt(lv_obj_t *obj){
    lv_obj_update_layout(lv_screen_active());lv_area_t area;lv_obj_get_coords(obj,&area);
    point={(area.x1+area.x2)/2,(area.y1+area.y2)/2};pressed=true;lv_indev_read(pointer);
}
static void releasePointer(){pressed=false;lv_indev_read(pointer);}
static void pointerClick(const char *value,unsigned hold=40){
    auto target=lv_obj_get_parent(find(value));
    lv_obj_scroll_to_view_recursive(target,LV_ANIM_OFF);lv_obj_update_layout(lv_screen_active());
    pressAt(target);advance(hold);releasePointer();advance(30);
}
static void watchMemory(){
    // Replay proceeds in visible frames, so a single large wall-time jump cannot skip it.
    for(unsigned i=0;i<400;++i)advance(30);
    assert(!lv_obj_has_state(lv_obj_get_parent(find("花")),LV_STATE_DISABLED));
}
static void emit(int pin,button_event_t event){auto &b=buttons[pin];b.event=event;assert(b.cb[event]);b.cb[event](&b,nullptr);}
static void single(int pin){emit(pin,BUTTON_PRESS_DOWN);emit(pin,BUTTON_PRESS_UP);emit(pin,BUTTON_SINGLE_CLICK);emit(pin,BUTTON_PRESS_END);advance();advance();}
static void bootBack(){
    install(g::create(73));open();click("照料");const auto oldVolume=volume;
    single(0);find("照料");assert(volume==oldVolume);single(0);assert(!app && volume==oldVolume);
    open();click("照料");click("喂食");const auto target=pending->state;
    single(0);find("照料");single(0);assert(!app && pending && !pending->cancelled);complete();
    open();assert(g::encode(saved.state,1)==g::encode(target,1));click("图鉴");click("花冠兔");single(0);find("成长图鉴");single(0);find("照料");
    click("照料");emit(0,BUTTON_PRESS_DOWN);close();open();click("照料");
    emit(0,BUTTON_PRESS_UP);emit(0,BUTTON_SINGLE_CLICK);emit(0,BUTTON_PRESS_END);advance();find("照料背包");assert(volume==oldVolume);
    close();emit(0,BUTTON_PRESS_DOWN);open();click("照料");emit(0,BUTTON_PRESS_UP);emit(0,BUTTON_SINGLE_CLICK);emit(0,BUTTON_PRESS_END);advance();find("照料背包");
    screen=false;single(0);screen=true;advance(1000);find("照料背包");assert(volume==oldVolume);
    single(4);assert(volume==oldVolume+5);emit(4,BUTTON_DOUBLE_CLICK);assert(shots==1);
    single(5);assert(!screen);single(5);assert(screen);
    events.clear();emit(0,BUTTON_PRESS_DOWN);emit(0,BUTTON_PRESS_UP);emit(0,BUTTON_PRESS_DOWN);emit(0,BUTTON_PRESS_UP);emit(0,BUTTON_DOUBLE_CLICK);emit(0,BUTTON_PRESS_END);
    assert(events.size()==1 && events[0].type==sys::EventType::HomeRequested);advance();find("照料背包");close();
    single(0);assert(volume==oldVolume); // Ordinary BOOT volume behavior is restored after exit.
    std::cout<<"PASS: production BOOT single returns/exits, detached save, album back, stale/outside/screen-off press fencing, BOOT double Home, unchanged KEY/PWR and restored volume.\n";
}
static void memoryPresentation(){
    auto state=g::create(73);g::apply(state,g::Action::Play);state.miniLength=3;state.melody={0,0,2,0,0,0};install(state);open();
    find("3");check();capture("memory-count-3",4);
    for(unsigned i=0;i<34;++i)advance(30);find("2");capture("memory-count-2",4);
    for(unsigned i=0;i<33;++i)advance(30);find("1");capture("memory-count-1",4);
    for(unsigned i=0;i<34;++i)advance(30);
    auto flower=lv_obj_get_parent(find("花"));assert(lv_obj_has_state(flower,LV_STATE_CHECKED));
    assert(lv_obj_get_style_opa(flower,LV_PART_MAIN)==LV_OPA_COVER);find("看顺序 · 1/3");capture("memory-bright",4);
    for(unsigned i=0;i<31;++i)advance(30);assert(!lv_obj_has_state(flower,LV_STATE_CHECKED));capture("memory-gap",4);
    for(unsigned i=0;i<10;++i)advance(30);assert(lv_obj_has_state(flower,LV_STATE_CHECKED));find("看顺序 · 2/3");
    const auto before=g::encode(saved.state,1);screen=false;advance(60000);screen=true;advance(1000);find("3");
    assert(g::encode(saved.state,1)==before);watchMemory();pointerClick("花");settle();assert(saved.state.miniPosition==1);
    pointerClick("重看");find("3");for(unsigned i=0;i<101;++i)advance(30);find("看顺序 · 2/3");watchMemory();
    pointerClick("花");settle();pointerClick("风");settle();assert(saved.state.stars==1 && saved.state.turn==1 && !saved.state.miniLength);close();
    // A stalled GUI cannot silently jump over the countdown or the melody.
    install(state);open();advance(60000);find("3");assert(lv_obj_has_state(lv_obj_get_parent(find("花")),LV_STATE_DISABLED));close();
    std::cout<<"PASS: visible 3-2-1, full-opacity cues, repeated-note gap, screen-off restart, partial replay and stalled-frame protection.\n";
}
static void homePanel(){
    const char *stages[]={"萌芽","幼年","少年","成年"};
    const unsigned turns[]={0,6,14,24};
    for(unsigned stage=0;stage<4;++stage){
        auto s=g::create(73);while(s.turn<turns[stage])g::apply(s,g::Action::Rest);
        s.fruit=s.stars=999;s.hunger=100;s.mood=0;s.energy=7;install(s);open();advance(2400);
        auto panel=lv_obj_get_parent(lv_obj_get_parent(find(stages[stage])));lv_area_t area;lv_obj_get_coords(panel,&area);
        assert(area.x1==48 && area.x2==235 && area.y1==4 && area.y2==48);
        unsigned assets=0,bars=0;
        const auto visit=[&](auto &&self,lv_obj_t *obj)->void{
            if(lv_obj_check_type(obj,&lv_label_class)){
                const auto text=lv_label_get_text(obj);assert(!std::strchr(text,'/') && !std::strchr(text,'%'));
                if(!std::strcmp(text,"999"))++assets;
            }
            if(lv_obj_check_type(obj,&lv_bar_class))++bars;
            assert(!lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE));
            for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)self(self,lv_obj_get_child(obj,i));
        };
        visit(visit,panel);
        assert(assets==2 && bars==3 && label(panel,"100") && label(panel,"0") && label(panel,"7"));
        auto pet=lv_obj_get_child(app->root(),1);assert(lv_obj_get_y(pet)==51 && lv_obj_get_height(pet)==135 && lv_obj_get_width(pet)==184);
        check();char filename[64];std::snprintf(filename,sizeof(filename),"nameplate-stage-%u",stage);capture(filename,4);
        const auto objectCount=objects(app->root()),timerCount=timers(),oldWrites=writes;
        const auto stateBefore=g::encode(saved.state,saved.revision);
        std::vector<unsigned char> header;
        for(int y=4;y<49;++y)header.insert(header.end(),pixels+(y*240+48)*2,pixels+(y*240+236)*2);
        for(unsigned i=0;i<8;++i)advance(100);
        std::snprintf(filename,sizeof(filename),"nameplate-motion-%u",stage);capture(filename,4);
        bool changed=false;for(int y=4;y<49;++y)changed|=std::memcmp(header.data()+(y-4)*188*2,pixels+(y*240+48)*2,188*2)!=0;
        assert(changed);
        const std::vector<unsigned char> frozen(pixels,pixels+sizeof(pixels));
        screen=false;advance(60000);lv_refr_now(nullptr);
        assert(std::memcmp(frozen.data(),pixels,sizeof(pixels))==0);screen=true;advance(1000);
        assert(objects(app->root())==objectCount && timers()==timerCount && oldWrites==writes && !pending);
        assert(g::encode(saved.state,saved.revision)==stateBefore);check();close();
    }
    // Every real form name fits the same banner, including both rare and ordinary forms.
    for(unsigned form=0;form<g::Forms;++form){
        auto s=g::create(42);while(s.turn<24)g::apply(s,g::Action::Rest);
        s.form=uint8_t(form);s.album|=uint16_t(1u<<form);install(s);open();find(form&1?g::RouteData[form/2].rare:g::RouteData[form/2].normal);check();close();
    }
    std::cout<<"PASS: compact 188x45 nameplate, stage-only labels, 999 assets and 0/7/100 attributes, four animated stages, pet hit area expanded to 184x135, noninteractive ornaments, screen-off pause, no state/write/object/timer growth and all 12 form names.\n";
}
static void touchRegression(){
    install(g::create(73));open();click("照料");click("喂食");assert(pending && pending->saving);
    bool deleted=false;auto back=lv_obj_get_parent(find("返回"));
    lv_obj_add_event_cb(back,[](lv_event_t *event){*static_cast<bool *>(lv_event_get_user_data(event))=true;},LV_EVENT_DELETE,&deleted);
    pressAt(back);complete();advance();const bool stable=!deleted;
    releasePointer();advance();const bool returned=label(app->root(),"照料")!=nullptr;close();
    install(g::create(73));open();click("照料");click("喂食");
    auto rest=lv_obj_get_parent(find("休息"));pressAt(rest);releasePointer();advance();
    settle();settle();const bool secondAction=saved.state.turn==2;close();
    std::cout<<"Touch regression: held target survives save="<<stable<<", release returns="<<returned
             <<", second tap during save persists="<<secondAction<<std::endl;
    assert(stable && returned && secondAction);
}
static void touchBursts(){
    // Real hit testing covers icons, captions, navigation and ordinary finger dwell.
    install(g::create(73));open();pointerClick("照料");
    auto expected=saved.state;const unsigned beforeWrites=writes;
    const char *care[]={"喂食","休息","清洁","陪伴","料理","休息","清洁","陪伴","休息"};
    const g::Action actions[]={g::Action::Feed,g::Action::Rest,g::Action::Clean,g::Action::Cuddle,g::Action::Cook,
                              g::Action::Rest,g::Action::Clean,g::Action::Cuddle,g::Action::Rest};
    for(unsigned i=0;i<9;++i){pointerClick(care[i]);assert(g::apply(expected,actions[i]).changed);}
    assert(pending && writes==beforeWrites);find("保存中 · 待处理 8");check();capture("touch-queued",4);
    auto rest=lv_obj_get_parent(find("休息"));assert(lv_obj_has_state(rest,LV_STATE_DISABLED));
    pressAt(rest);releasePointer();advance(); // Queue is bounded; a tenth press cannot grow it.
    for(unsigned i=0;i<9;++i)settle();
    assert(!pending && writes==beforeWrites+9 && g::encode(saved.state,1)==g::encode(expected,1));
    assert(!lv_obj_has_state(lv_obj_get_parent(find("休息")),LV_STATE_DISABLED));
    // Rejected actions also drain in order, with no extra save or permanently disabled control.
    for(unsigned i=0;i<9;++i)pointerClick("料理");
    for(unsigned i=0;i<9 && pending;++i)settle();
    assert(!pending);pointerClick("休息");assert(pending);settle();close();

    // A new physical press can overlap the pending redraw from the previous release.
    install(g::create(73));open();pointerClick("照料");
    pressAt(lv_obj_get_parent(find("喂食")));releasePointer();assert(pending);
    bool deleted=false;rest=lv_obj_get_parent(find("休息"));
    lv_obj_add_event_cb(rest,[](lv_event_t *event){*static_cast<bool *>(lv_event_get_user_data(event))=true;},LV_EVENT_DELETE,&deleted);
    pressAt(rest);advance(200);assert(!deleted);releasePointer();advance(30);settle();settle();
    assert(saved.state.turn==2);close();

    // Saving must neither reset album scrolling nor keep redrawing a static menu background.
    install(g::create(73));open();pointerClick("照料");pointerClick("喂食");pointerClick("返回");pointerClick("图鉴");
    auto list=lv_obj_get_parent(lv_obj_get_parent(find("花冠兔")));lv_obj_scroll_to_y(list,120,LV_ANIM_OFF);advance();
    const int scroll=lv_obj_get_scroll_y(list);assert(scroll>0);
    auto scene=lv_obj_get_child(lv_obj_get_child(app->root(),0),0);
    auto source=static_cast<const lv_image_dsc_t *>(lv_image_get_src(scene));
    ui::garden_scene::Pixels backdrop{};std::memcpy(backdrop.data(),source->data,sizeof(backdrop));
    settle();assert(lv_obj_get_scroll_y(list)==scroll);
    for(unsigned i=0;i<20;++i)advance(100);
    assert(std::memcmp(backdrop.data(),source->data,sizeof(backdrop))==0);
    pointerClick("花冠兔");find("花匠 · 未相遇");pointerClick("返回");pointerClick("返回");close();

    // Six rune inputs can arrive before any save completes, including repeated notes.
    auto memory=g::create(73);g::apply(memory,g::Action::Play);memory.miniLength=6;
    const uint8_t melody[]={0,0,2,1,3,0};for(unsigned i=0;i<6;++i)memory.melody[i]=melody[i];
    install(memory);open();auto note=lv_obj_get_parent(find("花"));assert(lv_obj_has_state(note,LV_STATE_DISABLED));
    pressAt(note);releasePointer();assert(!pending);watchMemory();
    const char *names[]={"花","雨","风","星"};
    for(auto value:melody)pointerClick(names[value]);
    find("待处理 5 · 保存中");check();capture("memory-queued",4);
    for(unsigned i=0;i<6;++i)settle();
    assert(!pending && !saved.state.miniLength && saved.state.turn==1 && saved.state.stars==1);close();

    // Navigation, replay, save failures and App destruction cannot replay old-page commands.
    install(g::create(73));open();pointerClick("照料");pointerClick("喂食");pointerClick("休息");pointerClick("返回");
    settle();advance();assert(saved.state.turn==1 && !pending);close();
    install(memory);open();watchMemory();pointerClick("花");pointerClick("花");pointerClick("重看");
    settle();watchMemory();assert(!pending && saved.state.miniPosition==1);close();
    install(g::create(73));open();pointerClick("照料");pointerClick("喂食");pointerClick("休息");ioError=EIO;
    settle();find("进度尚未保存");ioError=0;pointerClick("重试保存");settle();advance();assert(saved.state.turn==1 && !pending);close();
    install(g::create(73));open();pointerClick("照料");pointerClick("喂食");pointerClick("休息");close();complete();
    open();assert(saved.state.turn==1 && !pending);close();
    std::cout<<"PASS: real-pointer queued care/memory bursts, FIFO and eight-command bound, redraw during press, save-ack scroll stability, static menus, rejected-action drain, navigation/replay/error/exit cancellation.\n";
}
static void petInteraction(){
    auto state=g::create(73);state.weather=0;state.biome=0;install(state);open();advance(2400);
    const auto stored=g::encode(saved.state,saved.revision);const auto oldWrites=writes;
    const auto count=objects(app->root()),timerCount=timers();
    auto scene=lv_obj_get_child(lv_obj_get_child(app->root(),0),0);
    auto source=static_cast<const lv_image_dsc_t *>(lv_image_get_src(scene));
    ui::garden_scene::Pixels before{};std::memcpy(before.data(),source->data,sizeof(before));
    capture("pet-idle",4);tapPet();
    assert(std::memcmp(before.data(),source->data,sizeof(before))!=0);
    capture("pet-touch",4);advance(400);capture("pet-jump",4);
    // Twelve render steps restore the idle frame; a tap lasts 1.2 seconds.
    for(unsigned i=0;i<11;++i)advance(100);
    assert(std::memcmp(before.data(),source->data,sizeof(before))==0);
    capture("pet-settled",4);
    lv_mem_monitor_t memoryBefore{},memoryAfter{};lv_mem_monitor(&memoryBefore);
    for(unsigned i=0;i<1000;++i)tapPet();
    lv_mem_monitor(&memoryAfter);
    assert(objects(app->root())==count && timers()==timerCount);
    assert(memoryBefore.free_size==memoryAfter.free_size);
    assert(!pending && writes==oldWrites && g::encode(saved.state,saved.revision)==stored);
    screen=false;ui::garden_scene::Pixels off{};std::memcpy(off.data(),source->data,sizeof(off));advance(60000);
    assert(std::memcmp(off.data(),source->data,sizeof(off))==0);screen=true;advance(1000);
    assert(!pending && g::encode(saved.state,saved.revision)==stored);check();close();
}
int main(){
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
    sys::Buttons::init();pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,readPointer);
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);saved={};ioError=0;open();find("领养伙伴");check();capture("welcome",theme);
        click("领养伙伴");assert(pending && pending->saving);settle();check();capture("home",theme);
        click("照料");check();capture("care",theme);auto turn=saved.state.turn;click("喂食");settle();assert(saved.state.turn==turn+1);check();
        click("返回");click("培养");check();capture("train",theme);click("园艺");settle();assert(saved.state.affinity[0]>=14);
        click("返回");click("冒险");check();capture("explore",theme);click("海湾");settle();check();capture("encounter",theme);
        auto event=saved.state.event;assert(event!=g::None);close();open();assert(saved.state.event==event);check();
        char choice[100];auto &e=g::Encounters[event];std::snprintf(choice,sizeof(choice),"%s",e.left);
        click(choice);settle();assert(saved.state.event==g::None);check();
        click("照料");click("记忆");settle();check();capture("memory-show",theme);
        assert(saved.state.miniLength);watchMemory();check();capture("memory-input",theme);
        static const char *notes[]={"花","雨","风","星"};
        click(notes[saved.state.melody[0]]);settle();assert(saved.state.miniPosition==1);close();open();watchMemory();check();
        while(saved.state.miniLength){click(notes[saved.state.melody[saved.state.miniPosition]]);settle();}
        assert(saved.state.stars==1);click("家园");check();capture("house",theme);click("播种");settle();assert(saved.state.crop==0);
        click("返回");click("图鉴");check();capture("album",theme);click("花冠兔");check();capture("detail",theme);click("返回");click("返回");
        const auto snapshot=g::encode(saved.state,1);screen=false;advance(60*60*1000);assert(g::encode(saved.state,1)==snapshot);screen=true;advance(1000);check();
        // Save worker retains data after BOOT/global-home destroys all App objects.
        click("照料");click("休息");assert(pending && pending->saving);auto target=pending->state;close();assert(!pending->cancelled);complete();open();assert(g::encode(saved.state,1)==g::encode(target,1));close();
        // Fill an almost-adult real model and use the final action through touch.
        auto adult=g::create(99);while(adult.turn<23)g::apply(adult,g::Action::Cuddle);install(adult);open();click("照料");click("休息");settle();find("成长完成");check();capture("adult",theme);
        click("传承新生");check();capture("rebirth",theme);click("开启下一代");settle();assert(saved.state.generation==2 && saved.state.turn==0 && saved.state.album);check();close();
    }
    ui::theme::init(4);bootBack();memoryPresentation();homePanel();touchRegression();touchBursts();petInteraction();ioError=EBADMSG;open();find("花园无法打开");check();capture("bad-save",4);
    const auto oldWrites=writes;click("临时体验");click("领养伙伴");click("照料");click("喂食");assert(!pending && writes==oldWrites);check();close();
    ioError=0;install(g::create(17));open();click("照料");click("喂食");ioError=EIO;settle();find("进度尚未保存");check();capture("save-error",4);
    ioError=0;click("重试保存");settle();assert(saved.state.turn==1);close();
    // Busy open has a bounded retry and leaves a reachable exit.
    busy=true;open();advance(4000);find("花园无法打开");busy=false;click("重新读取");settle();check();close();
    const auto baseline=timers();
    for(unsigned i=0;i<50;++i){open();assert(timers()==baseline+1);close();assert(timers()==baseline);}
    // Every adult's actual pixel art is captured for inspection, including unlocked badges.
    auto all=g::create(42);while(all.turn<24)g::apply(all,g::Action::Rest);all.album=4095;install(all);open();click("图鉴");
    for(unsigned i=0;i<12;++i){char name[90],file[32];std::snprintf(name,sizeof(name),"%s",i&1?g::RouteData[i/2].rare:g::RouteData[i/2].normal);
        click(name);check();std::snprintf(file,sizeof(file),"form-%02u",i);capture(file,4);click("返回");}
    close();lv_indev_delete(pointer);lv_display_delete(display);lv_deinit();
    std::cout<<"PASS: production LVGL five themes, care/training/exploration/memory/farm/album/12 previews/rebirth, real pointer pet interaction, 1000 taps without writes or allocations, bounded effects and screen-off rendering, bounded sequential input, pending-event and partial-memory reopen, save-error retry, temporary mode, screen-off invariance, busy retry, detached save and 50 clean App lifecycles.\n";
}
