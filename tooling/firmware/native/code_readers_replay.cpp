#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <io.h>
#include "ui/code_view.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include "core/event_bus.hpp"
#include "core/button_mgr.hpp"
#include "core/power_policy.hpp"
#include "core/pattern_store.hpp"
#include "core/knowledge_store.hpp"
#define private public
#include "apps/design_patterns.hpp"
#include "apps/programming.hpp"
#undef private
#ifndef ESTALE
#define ESTALE 116
#endif
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {
unsigned held=0,workers=0,guards=0,allocations=0;
uint32_t key=0,view=0,next=0,sub=0;
std::map<uint32_t,std::pair<sys::EventType,sys::EventHandler>> handlers;
std::deque<std::pair<void (*)(void*),void *>> queue;
std::string path(const char *p){std::string s=p;CHECK(s.rfind("/sdcard/",0)==0);return "card/"+s.substr(8);}
FILE *open(const char *p,const char *m){CHECK(held);return std::fopen(path(p).c_str(),m);}
int stat(const char *p,struct stat *s){CHECK(held);return ::stat(path(p).c_str(),s);}
int rename(const char *a,const char *b){CHECK(held);return std::rename(path(a).c_str(),path(b).c_str());}
int unlink(const char *p){CHECK(held);return std::remove(path(p).c_str());}
void drain(){while(!queue.empty()){auto job=queue.front();queue.pop_front();job.first(job.second);}CHECK(!held&&!guards&&!workers);}
}
namespace core::sd {
Access requestAccess(bool resume){return {1,resume};}
bool busy(){return false;}
Session::Session(Access,bool,bool){held_=true;generation_=1;++fake::held;}
Session::~Session(){if(held_)--fake::held;}
void Session::reportError(int){}
bool ensure(const char *p){CHECK(fake::held);std::filesystem::create_directories(fake::path(p));return true;}
}
namespace sys {
struct Power {static bool isScreenOn(){return true;}static bool sleepPending(){return false;}
    struct WorkGuard {WorkGuard(){++fake::guards;}~WorkGuard(){--fake::guards;}};};
uint32_t EventBus::subscribe(EventType t,EventHandler h){fake::handlers[++fake::sub]={t,h};return fake::sub;}
void EventBus::unsubscribe(uint32_t id){fake::handlers.erase(id);}
void EventBus::publish(const Event &event){auto copy=fake::handlers;for(auto &h:copy)if(h.second.first==event.type)h.second.second(event);}
uint32_t Buttons::claimPageKeys(){return fake::key=++fake::next;}
void Buttons::releasePageKeys(uint32_t k){if(fake::key==k)fake::key=0;}
uint32_t Buttons::claimViewExit(){return fake::view=++fake::next;}
void Buttons::releaseViewExit(uint32_t k){if(fake::view==k)fake::view=0;}
bool Buttons::takeViewExit(uint32_t){return false;}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){}
void TextInput::bindField(lv_obj_t *){}
void TextInput::hide(){}
bool TextInput::visible(){return false;}
}
static uint32_t esp_random(){return 7;}
constexpr int pdPASS=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static int xTaskCreateWithCaps(void (*f)(void*),const char*,unsigned,void *arg,int,void*,int){fake::queue.emplace_back(f,arg);++fake::workers;return pdPASS;}
static void vTaskDeleteWithCaps(void*){CHECK(fake::workers);--fake::workers;}
static void vTaskDelay(int){}
static void *heap_caps_malloc(size_t size,int){auto p=std::malloc(size);if(p)++fake::allocations;return p;}
static void heap_caps_free(void *p){if(p){CHECK(fake::allocations);--fake::allocations;}std::free(p);}
#define fopen fake::open
#define stat(a,b) fake::stat(a,b)
#define rename fake::rename
#define unlink fake::unlink
#define fsync _commit
#include "patterns_store.inc"
#include "knowledge_store.inc"
#undef fopen
#undef stat
#undef rename
#undef unlink
#undef fsync
#include "patterns.inc"
#include "programming.inc"
namespace k=core::knowledge;
using P=apps::Programming;using D=apps::DesignPatterns;
static lv_indev_t *pointer=nullptr;
static bool touched=false;
static lv_point_t point{};
static uint8_t framebuffer[240*240*2];
static void advance(unsigned ms=20){fake::drain();lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static lv_obj_t *root(){auto r=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(r);lv_obj_set_size(r,240,240);return r;}
static void touch(int x,int y,bool down){point={x,y};touched=down;lv_tick_inc(20);lv_indev_read(pointer);lv_obj_update_layout(lv_screen_active());}
static void drag(int x,int y,int dx,int dy){touch(x,y,true);for(int i=1;i<=8;++i)touch(x+dx*i/8,y+dy*i/8,true);touch(x+dx,y+dy,false);for(int i=0;i<20;++i)advance();}
static void tap(int x,int y){touch(x,y,true);touch(x,y,false);advance();}
static void settle(P &a){for(unsigned i=0;i<4;++i){fake::drain();a.poll();advance();}}
static void settle(D &a){fake::drain();a.poll();advance();}
static void capture(const std::string &name){advance();lv_refr_now(nullptr);FILE *f=std::fopen(name.c_str(),"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(size_t i=0;i<sizeof(framebuffer);i+=2){const unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={static_cast<unsigned char>((c>>11)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}
static void stateTests(){
    CHECK(!k::State{}.wrap);
    k::State state;state.wrap=true;state.large=true;state.favorites={19,21};state.visit({19,123,415});
    k::StateRecord old{},now{};k::encodeStateInto(old,state,8);
    k::put32(old.data()+28,0);k::put32(old.data()+k::StateStride-4,k::crc32(old.data(),k::StateStride-4));
    CHECK(k::validState(old));auto loaded=k::decodeState(old);
    CHECK(!loaded.wrap&&loaded.large&&loaded.favorites==state.favorites);
    CHECK(loaded.recent.size()==1&&loaded.recent[0].offset==415&&loaded.recent[0].section==123);
    for(bool wrap:{false,true}){loaded.wrap=wrap;k::encodeStateInto(now,loaded,9);CHECK(k::decodeState(now).wrap==wrap);CHECK(k::latestState(old,now)==1);}
    CHECK(ui::code::commonIndent("\n    a\n      b\n    c")==4);
    CHECK(ui::code::unindent("\n    a\n      b\n    c",4)=="\na\n  b\nc");
    CHECK(ui::code::commonIndent("\n \n")==0);
    CHECK(ui::code::unindent("\" ; {} \"",0)=="\" ; {} \"");
}
static void programming(unsigned theme,bool pictures){
    auto r=root();P a;a.onCreate(r);a.onShow();settle(a);
    CHECK(a.m_page==P::Page::Home&&a.m_library&&!a.m_state.wrap);
    CHECK(lv_obj_get_child_count(a.m_body)==6);
    const char *categories[]={"Java","JVM","算法","MySQL","组件","工具"};
    for(unsigned i=0;i<6;++i){
        auto b=lv_obj_get_child(a.m_body,i);
        CHECK(std::string(lv_label_get_text(lv_obj_get_child(b,0)))==categories[i]);
        CHECK(lv_obj_get_x(b)==int(i%2)*120&&lv_obj_get_y(b)==int(i/2)*56);
        CHECK(lv_obj_get_width(b)==119&&lv_obj_get_height(b)==55);
    }
    CHECK(lv_obj_get_child_count(a.m_footer)==3);
    drag(120,160,0,-100);CHECK(lv_obj_get_scroll_y(a.m_body)==0&&a.m_page==P::Page::Home);
    if(pictures)capture("reference-home-"+std::to_string(theme)+".ppm");
    a.m_mode=P::Mode::Search;a.m_query="";a.filter();a.m_page=P::Page::List;a.m_offset=0;a.draw();advance();
    CHECK(a.count()==a.m_library->count&&a.count()==449);
    CHECK(lv_obj_get_child_count(a.m_body)==8); // spacer + seven recycled touch rows
    CHECK(lv_obj_is_hidden(a.m_footer));
    drag(120,165,0,-105);CHECK(a.m_offset>40);
    auto oldY=lv_obj_get_scroll_y(a.m_body);drag(190,110,-130,0);CHECK(lv_obj_get_scroll_y(a.m_body)==oldY);
    lv_obj_scroll_to_y(a.m_body,100000,LV_ANIM_OFF);advance();
    bool last=false;for(auto button:a.m_pool)if(!lv_obj_is_hidden(button)){
        auto text=std::string(lv_label_get_text(lv_obj_get_child(button,0)));if(text.rfind("449. ",0)==0)last=true;
    }CHECK(last);CHECK(lv_obj_get_child_count(a.m_body)==8);
    if(pictures)capture("reference-list-"+std::to_string(theme)+".ppm");
    // Every article must render both modes and retain all original sections.
    bool found=false;unsigned longEntry=0,codeSection=0,noCode=0;
    for(unsigned i=0;i<a.m_library->count;++i){
        if(!pictures && found && noCode)break;
        a.request(i);settle(a);CHECK(a.m_page==P::Page::Reader);
        auto title=lv_obj_get_child(a.m_header,1);auto titleFont=lv_obj_get_style_text_font(title,LV_PART_MAIN);
        lv_text_attributes_t titleAttributes;lv_text_attributes_init(&titleAttributes);
        std::string titleText=lv_label_get_text(title);
        CHECK(lv_label_get_long_mode(title)==LV_LABEL_LONG_MODE_CLIP);
        CHECK(lv_obj_get_height(title)==titleFont->line_height);
        CHECK(lv_text_get_width(titleText.c_str(),titleText.size(),titleFont,&titleAttributes)<=80);
        CHECK(k::utf8(titleText.data(),titleText.size()));
        CHECK(std::string(a.m_library->entry(i).shortTitle).rfind(titleText,0)==0);
        if(pictures&&std::string(a.m_library->entry(i).title)=="帮助与PowerShell版本")capture("reference-title-"+std::to_string(theme)+".ppm");
        CHECK(a.m_blocks.size()==a.m_article.sections.size());
        CHECK(a.m_positions.size()==std::count_if(a.m_article.sections.begin(),a.m_article.sections.end(),[](const auto &s){return !s.kind;}));
        if(!a.m_hasCode){noCode=i;CHECK(lv_obj_get_child_count(a.m_header)==4);}
        else {
            CHECK(lv_obj_get_child_count(a.m_header)==5);a.act(P::Action::Toggle);advance();CHECK(a.m_code);
            CHECK(a.m_positions.size()==std::count_if(a.m_article.sections.begin(),a.m_article.sections.end(),[](const auto &s){return s.kind!=0;}));
            if(!found&&a.m_codeViewport&&lv_obj_get_scroll_right(a.m_codeViewport)>150&&lv_obj_get_scroll_bottom(a.m_body)>100){
                found=true;longEntry=i;codeSection=a.m_positions.front().section;
            }
            a.act(P::Action::Toggle);advance();CHECK(!a.m_code);
        }
    }
    CHECK(found);a.request(longEntry,codeSection);settle(a);CHECK(a.m_code);
    const auto original=a.m_blocks;
    auto viewport=a.m_codeViewport;auto container=lv_obj_get_parent(viewport);auto gutter=lv_obj_get_child(container,1);
    auto displayed=lv_obj_get_child(viewport,0);const auto gutterX=lv_obj_get_x(gutter);
    CHECK(lv_obj_get_style_text_font(displayed,LV_PART_MAIN)==&lv_font_code_13);
    if(pictures)capture("reference-"+std::to_string(theme)+".ppm");
    drag(213,112,-130,0);CHECK(a.m_page==P::Page::Reader&&a.m_code);
    CHECK(lv_obj_get_scroll_x(a.m_codeViewport)>50&&a.m_codeX>50);CHECK(lv_obj_get_x(gutter)==gutterX);
    // Stop touch inertia and clamp an end-of-drag overshoot before comparing
    // the stable reading position across reconstruction.
    lv_obj_scroll_to_x(a.m_codeViewport,lv_obj_get_scroll_x(a.m_codeViewport),LV_ANIM_OFF);advance();
    const auto x=lv_obj_get_scroll_x(a.m_codeViewport);
    auto id=a.m_library->entry(a.m_entry).id;auto favorite=a.m_state.favorite(id);
    a.act(P::Action::Favorite);advance();CHECK(a.m_state.favorite(id)!=favorite);
    if(lv_obj_get_scroll_x(a.m_codeViewport)!=x)std::fprintf(stderr,"x before=%ld after=%ld saved=%ld right=%ld width=%ld src=%ld\n",long(x),long(lv_obj_get_scroll_x(a.m_codeViewport)),long(a.m_codeX),long(lv_obj_get_scroll_right(a.m_codeViewport)),long(lv_obj_get_width(a.m_codeViewport)),long(lv_obj_get_width(lv_obj_get_child(a.m_codeViewport,0))));
    CHECK(lv_obj_get_scroll_x(a.m_codeViewport)==x);
    a.act(P::Action::Menu);a.onBack();advance();CHECK(lv_obj_get_scroll_x(a.m_codeViewport)==x);
    auto y=lv_obj_get_scroll_y(a.m_body);drag(145,165,0,-95);CHECK(lv_obj_get_scroll_y(a.m_body)>y+20);
    y=lv_obj_get_scroll_y(a.m_body);sys::EventBus::publish({sys::EventType::PreviousPage,int32_t(a.m_keys)});advance();CHECK(lv_obj_get_scroll_y(a.m_body)<y);
    a.act(P::Action::Toggle);advance();CHECK(!a.m_code);CHECK(a.m_positions.size()>=3);
    drag(145,165,0,-95);auto descriptionY=lv_obj_get_scroll_y(a.m_body);CHECK(descriptionY>0);
    a.act(P::Action::Toggle);a.act(P::Action::Toggle);advance();
    if(lv_obj_get_scroll_y(a.m_body)!=descriptionY)std::fprintf(stderr,"description before=%ld after=%ld stored=%ld bottom=%ld\n",long(descriptionY),long(lv_obj_get_scroll_y(a.m_body)),long(a.m_scroll[0]),long(lv_obj_get_scroll_bottom(a.m_body)));
    CHECK(lv_obj_get_scroll_y(a.m_body)==descriptionY);
    if(pictures)capture("reference-description-"+std::to_string(theme)+".ppm");
    a.act(P::Action::Menu);a.act(P::Action::Full);advance();CHECK(a.m_full);CHECK(lv_obj_get_height(a.m_body)==240);
    tap(130,100);CHECK(!a.m_full);
    a.act(P::Action::Toggle);a.act(P::Action::Menu);a.act(P::Action::Wrap);a.onBack();advance();CHECK(a.m_state.wrap);
    CHECK(lv_obj_get_scroll_right(a.m_codeViewport)<=0);CHECK(a.m_blocks==original);
    a.act(P::Action::Menu);a.act(P::Action::Wrap);a.onBack();advance();CHECK(!a.m_state.wrap);
    a.mark();auto saved=a.m_state.recent.front();a.request(longEntry,0,saved.offset,saved.section);settle(a);CHECK(a.m_code);
    a.request(noCode);settle(a);CHECK(!a.m_hasCode&&!a.m_code);a.act(P::Action::Toggle);CHECK(!a.m_code);
    // Cancellation must release the in-flight article snapshot as well.
    a.request(longEntry);a.onBack();settle(a);CHECK(!a.m_job);
    a.flush();fake::drain();a.onHide();a.onDestroy();lv_obj_delete(r);advance();
    CHECK(a.m_blocks.capacity()==0&&a.m_positions.capacity()==0&&!a.m_library&&!a.m_job&&!a.m_save&&!a.m_codeViewport);
    CHECK(!fake::allocations&&!fake::key&&!fake::view&&fake::handlers.empty()&&k::idle());
}
static void patterns(unsigned theme,bool pictures){
    auto r=root();D a;a.onCreate(r);a.onShow();settle(a);
    bool dragged=false;
    for(unsigned id=0;id<core::patterns::LessonCount;++id){
        a.request(id,0);settle(a);CHECK(a.lesson);a.act(D::Action::Start);advance();
        a.act(D::Action::Code);advance();CHECK(a.page==D::Page::Code);
        auto viewport=lv_obj_get_child(a.body,0);auto gutter=lv_obj_get_child(a.body,1);
        auto text=lv_obj_get_child(viewport,0);auto numbers=lv_obj_get_child(gutter,0);
        CHECK(std::string(lv_label_get_text(text))==ui::code::unindent(a.lesson->code,ui::code::commonIndent(a.lesson->code)));
        CHECK(lv_obj_get_height(text)==lv_obj_get_height(numbers));
        if(!dragged&&lv_obj_get_scroll_right(viewport)>0&&lv_obj_get_scroll_bottom(viewport)>40){
            dragged=true;
            if(pictures)capture("patterns-"+std::to_string(theme)+".ppm");
            drag(218,105,-125,0);CHECK(lv_obj_get_scroll_x(viewport)>40);CHECK(lv_obj_get_x(gutter)==0);
            drag(145,165,0,-95);CHECK(lv_obj_get_scroll_y(viewport)>40);
            CHECK(lv_obj_get_y(numbers)==ui::code::Top-lv_obj_get_scroll_y(viewport));
            if(pictures)capture("patterns-scroll-"+std::to_string(theme)+".ppm");
        }
        a.onBack();advance();CHECK(a.page==D::Page::Workshop);
    }
    CHECK(dragged);
    a.onHide();a.onDestroy();lv_obj_delete(r);advance();
    CHECK(!a.lesson&&!a.historySize&&!fake::key&&fake::handlers.empty());
    CHECK(core::patterns::idle());
}
int main(){
    stateTests();lv_init();auto display=lv_display_create(240,240);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t *d){d->point=point;d->state=touched?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
    unsigned baseTimers=timers();std::array<size_t,5> baseline{};int themes[]={4,5,6,8,10};
    for(unsigned cycle=0;cycle<15;++cycle){
        auto index=cycle%5;ui::theme::init(themes[index]);advance();
        programming(themes[index],cycle<5);patterns(themes[index],cycle<5);
        CHECK(timers()==baseTimers);
        if(cycle>=10)CHECK(memory()>=baseline[index]);baseline[index]=memory();
    }
    lv_indev_delete(pointer);lv_display_delete(display);lv_deinit();
    std::printf("%u checks passed: original six-tile home, single-line UTF-8 header, 449-article vertical reference, seven recycled rows, five-point favorite, descriptions/code toggle, no-code articles, original lines, horizontal code touch, vertical scroll, full-screen tap, independent positions, wrap, legacy bookmarks, 26 lessons x 5 themes, 30 App lifecycles and resource cleanup. Synthetic hardware only.\n",checks);
}
