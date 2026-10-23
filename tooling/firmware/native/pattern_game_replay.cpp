#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <io.h>
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/code_view.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include "core/event_bus.hpp"
#include "core/button_mgr.hpp"
#include "core/power_policy.hpp"
#include "core/pattern_store.hpp"
#define private public
#include "apps/design_patterns.hpp"
#undef private
#ifndef ESTALE
#define ESTALE 116
#endif
static unsigned checks=0,home=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {
bool screen=true,present=true,sleep=false,failTask=false;
uint32_t epoch=1,keys=0,nextKey=0,sub=0;uint64_t generation=1;unsigned held=0,workers=0,guards=0;
std::map<uint32_t,std::pair<sys::EventType,sys::EventHandler>> handlers;
void (*pending)(void*)=nullptr;
std::string path(const char *p){std::string s=p;CHECK(s.rfind("/sdcard/",0)==0);return "card/"+s.substr(8);}
FILE *open(const char *p,const char *m){CHECK(held);return std::fopen(path(p).c_str(),m);}
int stat(const char *p,struct stat *s){CHECK(held);return ::stat(path(p).c_str(),s);}
int rename(const char *a,const char *b){CHECK(held);return std::rename(path(a).c_str(),path(b).c_str());}
int unlink(const char *p){CHECK(held);return std::remove(path(p).c_str());}
void drain(){if(pending){auto work=pending;pending=nullptr;work(nullptr);}CHECK(!held&&!guards);}
void emit(sys::EventType type,int32_t token=0){auto copy=handlers;for(auto &h:copy)if(h.second.first==type){sys::Event e{type};e.i32=token;h.second.second(e);}}
}
namespace core::sd {
Access requestAccess(bool resume){return {fake::epoch,resume};}
Session::Session(Access a,bool,bool drain){if(fake::present&&a.epoch==fake::epoch&&(!fake::sleep||drain)){held_=true;generation_=fake::generation;++fake::held;}else error_=ENODEV;}
Session::~Session(){if(held_)--fake::held;}
void Session::reportError(int){}
bool ensure(const char *p){CHECK(fake::held);if(!managedDirectory(p)){errno=EINVAL;return false;}std::filesystem::create_directories(fake::path(p));return true;}
}
namespace sys {
struct Power {static bool isScreenOn(){return fake::screen;}static bool sleepPending(){return fake::sleep;}
struct WorkGuard {WorkGuard(){++fake::guards;}~WorkGuard(){--fake::guards;}};};
uint32_t EventBus::subscribe(EventType t,EventHandler h){fake::handlers[++fake::sub]={t,h};return fake::sub;}
void EventBus::unsubscribe(uint32_t id){fake::handlers.erase(id);}
uint32_t Buttons::claimPageKeys(){return fake::keys=++fake::nextKey;}
void Buttons::releasePageKeys(uint32_t k){if(fake::keys==k)fake::keys=0;}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){++home;}
void TextInput::bindField(lv_obj_t *){}
}
static uint32_t esp_random(){static uint32_t v=923487;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
constexpr int pdPASS=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static int xTaskCreateWithCaps(void (*f)(void*),const char*,unsigned,void*,int,void*,int){if(fake::failTask)return 0;CHECK(!fake::pending);fake::pending=f;++fake::workers;return pdPASS;}
static void vTaskDeleteWithCaps(void*){CHECK(fake::workers);--fake::workers;}
static void vTaskDelay(int){}
#define fopen fake::open
#define stat(a,b) fake::stat(a,b)
#define rename fake::rename
#define unlink fake::unlink
#define fsync _commit
#include "store.inc"
#undef fopen
#undef stat
#undef rename
#undef unlink
#undef fsync
#include "patterns.inc"
namespace p=core::patterns;
using App=apps::DesignPatterns;using Page=App::Page;using Action=App::Action;
static void models(){
 CHECK(p::PackageBytes>p::IndexBytes);CHECK(p::get32(p::Package+8)==26);
 CHECK(p::crc32(p::Package+16,26*16)==p::get32(p::Package+12));
 for(unsigned id=0;id<26;++id){auto row=p::Package+16+id*16;auto offset=p::get32(row),n=p::get32(row+4);p::Lesson l;
  CHECK(offset+n<=p::PackageBytes);CHECK(p::crc32(p::Package+offset,n)==p::get32(row+8));
  CHECK(p::parseLesson(reinterpret_cast<const char*>(p::Package+offset),n,id,l));
  CHECK(!p::parseLesson(reinterpret_cast<const char*>(p::Package+offset),n-1,id,l));
  CHECK(!p::parseLesson(reinterpret_cast<const char*>(p::Package+offset),n,(id+1)%26,l));
  CHECK(p::parseLesson(reinterpret_cast<const char*>(p::Package+offset),n,id,l));
  for(unsigned s=0;s<3;++s){CHECK(!l.runs[s].output.empty());CHECK(!l.runs[s].trace.empty());}
 }
 p::Progress progress;progress.started=true;progress.lesson=25;progress.stage=2;progress.design=1;progress.option=2;progress.complete.fill(7);
 auto good=p::encode(progress,0xffffffffu);CHECK(p::valid(good));CHECK(p::latest(good,p::encode(progress,0))==1);
 for(unsigned bit=0;bit<128*8;++bit){auto bad=good;bad[bit/8]^=1u<<(bit%8);CHECK(!p::valid(bad));CHECK(p::latest(bad,good)==1);}
 for(unsigned n=0;n<128;++n){auto torn=good;std::fill(torn.begin()+n,torn.end(),0);CHECK(p::valid(p::latest(torn,good)==0?torn:good));if(torn!=good)CHECK(p::latest(torn,good)==1);}
 CHECK(p::decode(good).complete==progress.complete);CHECK(p::decode(good).lesson==25);
}
static void store(){
 fake::present=false;auto noCard=p::open();fake::drain();CHECK(noCard->done&&noCard->error&&!noCard->generation);
 auto fallback=p::read(0,0);fake::drain();CHECK(fallback->done&&!fallback->error&&fallback->lesson&&fallback->fallback);
 fake::present=true;auto first=p::open();CHECK(!p::idle());fake::drain();CHECK(p::idle()&&!first->error&&first->generation==1);
 CHECK(std::filesystem::file_size("card/patterns/lessons.ypg")==p::PackageBytes);
 auto read=p::read(0,1);fake::drain();CHECK(!read->error&&!read->fallback&&read->lesson);
 FILE *f=std::fopen("card/patterns/lessons.ypg","r+b");CHECK(f);std::fputc('X',f);std::fclose(f);
 read=p::read(1,1);fake::drain();CHECK(!read->error&&read->fallback&&read->lesson);
 auto reopen=p::open();fake::drain();f=std::fopen("card/patterns/lessons.ypg","rb");CHECK(std::fgetc(f)=='X');std::fclose(f); // Never overwrite an existing package.
 p::Progress a;a.started=true;a.lesson=3;a.complete[3]=1;auto save=p::save(a,1,core::sd::requestAccess());fake::drain();CHECK(!save->error);
 a.complete[3]=3;save=p::save(a,1,core::sd::requestAccess());fake::drain();CHECK(!save->error&&std::filesystem::file_size("card/patterns/progress.dat")==1024);
 auto loaded=p::open();fake::drain();CHECK(loaded->progress.complete[3]==3);
 // Tear the newest slot; the older committed slot remains readable.
 f=std::fopen("card/patterns/progress.dat","r+b");CHECK(f);std::fseek(f,512,SEEK_SET);std::fputc('X',f);std::fclose(f);
 loaded=p::open();fake::drain();CHECK(!loaded->error&&loaded->progress.complete[3]==1);
 a.complete[3]=7;save=p::save(a,1,core::sd::requestAccess());++fake::generation;fake::drain();CHECK(save->error==ESTALE);
 auto cancelled=p::read(3,2);cancelled->cancelled=true;fake::drain();CHECK(cancelled->error==ECANCELED&&!cancelled->lesson);
 auto older=p::read(0,2);auto newer=p::read(1,2);CHECK(older->done&&older->cancelled);fake::drain();CHECK(newer->done&&newer->lesson);
 auto queued=p::save(a,2,core::sd::requestAccess());a.complete[4]=1;auto latest=p::save(a,2,core::sd::requestAccess());CHECK(queued->done&&queued->cancelled);fake::sleep=true;fake::drain();CHECK(!latest->error);fake::sleep=false;
 loaded=p::open();fake::drain();CHECK(loaded->progress.complete[4]==1);
 auto stale=p::save(a,2,core::sd::requestAccess());++fake::epoch;fake::drain();CHECK(stale->error);
 fake::failTask=true;CHECK(!p::open()&&p::idle());fake::failTask=false;
 CHECK(!fake::workers&&!fake::held&&!fake::pending);
}
static uint8_t framebuffer[240*240*2];
static lv_indev_t *pointer=nullptr;static bool touched=false;static lv_point_t point{};
static void advance(unsigned ms=20){fake::drain();lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void settle(App &a){fake::drain();a.poll();advance();}
static lv_obj_t *find(lv_obj_t *o,Action a,unsigned v){
 for(unsigned i=0;i<lv_obj_get_child_count(o);++i){auto c=lv_obj_get_child(o,i);if(lv_obj_check_type(c,&lv_button_class)&&uintptr_t(lv_obj_get_user_data(c))==(unsigned(a)|(v<<8)))return c;if(auto r=find(c,a,v))return r;}return nullptr;
}
static void click(App &a,Action action,unsigned v=0){auto b=find(a.parent,action,v);CHECK(b&&!lv_obj_has_state(b,LV_STATE_DISABLED));lv_obj_update_layout(b);if(lv_obj_get_height(b)<36||lv_obj_get_width(b)<36)std::fprintf(stderr,"action=%u page=%u size=%dx%d\n",unsigned(action),unsigned(a.page),int(lv_obj_get_width(b)),int(lv_obj_get_height(b)));CHECK(lv_obj_get_height(b)>=36&&lv_obj_get_width(b)>=36);lv_obj_send_event(b,LV_EVENT_CLICKED,nullptr);advance();}
static void capture(const char *name){advance();lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(size_t i=0;i<sizeof(framebuffer);i+=2){const unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={static_cast<unsigned char>((c>>11)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}
static void finish(App &a){for(unsigned i=0;a.page==Page::Running;++i){CHECK(i<50);advance(100);a.poll();}}
static void tap(lv_obj_t *b){CHECK(b);lv_obj_update_layout(b);lv_area_t r{};lv_obj_get_coords(b,&r);point={(r.x1+r.x2)/2,(r.y1+r.y2)/2};touched=true;lv_tick_inc(20);lv_indev_read(pointer);touched=false;lv_tick_inc(80);lv_indev_read(pointer);advance();}
static void labels(lv_obj_t *o){
 for(unsigned i=0;i<lv_obj_get_child_count(o);++i){auto c=lv_obj_get_child(o,i);if(lv_obj_check_type(c,&lv_label_class)){
   auto font=lv_obj_get_style_text_font(c,LV_PART_MAIN);uint32_t at=0;const auto s=lv_label_get_text(c);while(s[at]){uint32_t cp=lv_text_encoded_next(s,&at);if(cp<=32)continue;lv_font_glyph_dsc_t g{};CHECK(lv_font_get_glyph_dsc(font,&g,cp,0)&&!g.is_placeholder);}
   if(lv_obj_check_type(o,&lv_button_class)){if(lv_obj_get_height(c)>lv_obj_get_height(o))std::fprintf(stderr,"clipped: %s h=%d max=%d\n",s,int(lv_obj_get_height(c)),int(lv_obj_get_height(o)));CHECK(lv_obj_get_height(c)<=lv_obj_get_height(o));}
  }labels(c);}
}

static void gameModels(){
 using G=p::play::Game;
 for(unsigned id=0;id<26;++id)for(unsigned stage=0;stage<3;++stage){
  G g;g.reset(id,stage);CHECK(!g.won);
  for(unsigned node=0;node<4;++node)if(!g.editable(node)){const auto state=g.v;g.act(node);CHECK(g.v==state);}
  const auto plan=p::play::solution(id,stage);CHECK(plan.size&&plan.size<=16);
  for(unsigned i=0;i<plan.size;++i){CHECK(!g.won);CHECK(g.hint()>=0);g.act(unsigned(plan.actions[i]));CHECK(g.route.size<=8);for(unsigned j=0;j<g.route.size;++j)CHECK(g.route.nodes[j]<4);}
  if(!g.won)std::fprintf(stderr,"unsolved: id=%u stage=%u %s\n",id,stage,g.message);
  CHECK(g.won&&g.goal());const auto state=g.v;g.act(0);CHECK(g.v==state);
 }
 G g;
 // Commands must not execute at bind time, including rebinding after On.
 g.reset(16,2);g.act(0);CHECK(!g.v[2]&&!g.v[3]);g.act(4);CHECK(g.v[2]);g.act(1);CHECK(g.v[2]);g.act(4);CHECK(!g.v[2]);
 // Copies, cursors, and snapshots keep independent/opaque state.
 g.reset(7,2);g.act(2);CHECK(g.error&&!g.v[0]);g.act(1);g.act(2);CHECK(g.v[1]==2);
 g.reset(18,2);g.act(1);g.act(1);g.act(2);g.act(3);CHECK(g.v[0]==2&&!g.v[1]);
 g.reset(20,1);g.act(1);g.act(0);CHECK(g.v[1]==0&&g.v[0]==1);g.act(2);CHECK(g.v[0]==0);
 // Retired observers retain the last display but get no new notifications.
 g.reset(21,2);g.act(1);g.act(0);g.act(4);CHECK(g.v[3]==7&&g.v[4]==9&&g.won);
 // Invalid family assemblies and missing adapters cannot pass.
 g.reset(5,1);g.act(1);g.act(0);g.act(2);g.act(4);CHECK(g.error&&!g.won&&!g.v[3]);
 g.reset(8,1);g.act(0);g.act(4);CHECK(g.error&&!g.won&&!g.v[2]);
 // Order-independent valid assembly is accepted, no answer-sequence matching.
 g.reset(5,0);g.act(2);g.act(1);g.act(4);CHECK(g.won);
 g.reset(17,0);g.act(3);g.act(2);g.act(1);g.act(4);CHECK(g.won);
 // Roles stay separate: context change does not mutate expression nodes.
 g.reset(17,2);CHECK(g.v[1]&&g.v[2]&&g.v[3]);g.act(0);CHECK(g.v[1]&&g.v[2]&&g.v[3]);
 // Bounded random exploration exercises wrong operations, resets and values.
 for(unsigned id=0;id<26;++id){g.reset(id,2);for(unsigned j=0;j<800;++j){if(g.won||j%30==29)g.reset(id,2);g.act(esp_random()%5);for(unsigned n=0;n<4;++n){char value[64];g.value(n,value,sizeof(value));CHECK(std::strlen(value)<sizeof(value));}CHECK(g.route.size<=8);}}
}
static void uiTest(){
 lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t *d){d->point=point;d->state=touched?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 const auto baseTimers=timers();std::array<size_t,5> baseline{};
 for(unsigned cycle=0;cycle<20;++cycle){int themes[]={4,5,6,8,10};ui::theme::init(themes[cycle%5]);advance();
  auto root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_size(root,240,240);
  App a;a.onCreate(root);a.onShow();settle(a);CHECK(a.ready&&a.page==Page::Home&&fake::keys);
  labels(root);tap(find(root,Action::Help,0));CHECK(a.page==Page::Help);tap(find(root,Action::Back,0));CHECK(a.page==Page::Home);
  if(cycle<5){char name[50];std::sprintf(name,"home-%d.ppm",themes[cycle]);capture(name);}
  for(unsigned id=0;id<(cycle<2?26:1);++id){a.atlas=false;a.request(id,0);settle(a);CHECK(a.lesson&&a.page==Page::Brief);
   for(unsigned s=0;s<3;++s){CHECK(a.stage==s&&a.page==Page::Brief);labels(root);click(a,Action::Start);CHECK(a.page==Page::Workshop);
    if(cycle==0){char name[60];std::sprintf(name,"scene-%02u-%u.ppm",id,s);capture(name);}
    labels(root);CHECK(a.hints==(s==0));
    click(a,Action::Info);CHECK(a.page==Page::Info);click(a,Action::Code);CHECK(a.page==Page::Code);click(a,Action::Back);CHECK(a.page==Page::Info);click(a,Action::Back);CHECK(a.page==Page::Workshop);
    click(a,Action::Hint);CHECK(a.hints);
    const auto plan=p::play::solution(id,s);
    for(unsigned j=0;j<plan.size;++j){
     unsigned input=unsigned(plan.actions[j]);CHECK(a.page==Page::Workshop);
     if(cycle==0&&s==0)tap(find(root,input==4?Action::Run:Action::Node,input==4?0:input));
     else click(a,input==4?Action::Run:Action::Node,input==4?0:input);
     if(a.page==Page::Running){CHECK(!a.allowsQuickPanel());auto state=a.game.v;const auto bytes=memory();const auto objects=lv_obj_get_child_count(a.board);advance(40);a.poll();CHECK(a.game.v==state&&lv_obj_get_child_count(a.board)==objects&&memory()>=bytes);}
     finish(a);labels(root);
    }
    CHECK(a.passed&&a.page==Page::Result&&(a.progress.complete[id]&(1u<<s)));
    if(cycle==0&&id==21){char name[60];std::sprintf(name,"result-%u.ppm",s);capture(name);}
    if(s<2)click(a,Action::Advance);
   }
   CHECK(a.progress.complete[id]==7);
  }
  // Hardware key ownership, undo, errors, pause/resume and cancellation.
  a.request(21,0);settle(a);click(a,Action::Start);click(a,Action::Node,1);finish(a);CHECK(a.game.v[1]);
  fake::emit(sys::EventType::PreviousPage,a.keys-1);CHECK(a.game.v[1]);fake::emit(sys::EventType::PreviousPage,a.keys);CHECK(!a.game.v[1]);
  click(a,Action::Node,1);CHECK(a.page==Page::Running);const auto state=a.game.v;fake::screen=false;fake::emit(sys::EventType::ScreenOff);advance(5000);a.poll();CHECK(a.paused&&a.game.v==state&&a.page==Page::Running);
  fake::screen=true;advance(5000);CHECK(a.paused);click(a,Action::Resume);finish(a);CHECK(a.page==Page::Workshop);
  click(a,Action::Run);finish(a);CHECK(a.page==Page::Result);
  click(a,Action::Retry);click(a,Action::Start);click(a,Action::Run);finish(a);CHECK(a.page==Page::Workshop&&!a.game.won);click(a,Action::Hint);CHECK(a.highlighted==1);
  click(a,Action::Info);click(a,Action::Reset);CHECK(a.page==Page::Brief&&!a.historySize);
  // Leaving a successful animation must retain the completed delivery.
  a.request(16,0);settle(a);click(a,Action::Start);click(a,Action::Node,0);finish(a);click(a,Action::Run);CHECK(a.page==Page::Running&&a.game.won);
  click(a,Action::Back);CHECK(a.page==Page::Result&&a.passed);
  click(a,Action::Retry);click(a,Action::Start);click(a,Action::Node,0);finish(a);click(a,Action::Run);click(a,Action::Info);CHECK(a.page==Page::Info&&a.infoReturn==Page::Result&&a.passed);click(a,Action::Practice);CHECK(a.page==Page::Result);
  auto draft=a.progress;click(a,Action::Back);click(a,Action::Back);click(a,Action::Back);CHECK(a.page==Page::Home);
  click(a,Action::Atlas);click(a,Action::Category,2);click(a,Action::Entry,8);settle(a);CHECK(a.page==Page::Info&&a.atlas);
  CHECK(a.progress.lesson==draft.lesson);click(a,Action::Code);click(a,Action::Back);click(a,Action::Back);CHECK(a.page==Page::List);
  click(a,Action::Back);click(a,Action::Back);click(a,Action::Continue);settle(a);CHECK(a.index==draft.lesson&&!a.atlas);
  fake::present=false;a.generation=0;a.progress.complete[25]=7;a.dirty=true;a.flush();CHECK(!a.dirty);
  a.page=Page::Home;a.draw();click(a,Action::Help);fake::present=true;click(a,Action::Reload);settle(a);CHECK(a.generation==fake::generation&&a.progress.complete[25]==7);
  a.request(5,0);auto job=a.job;a.onHide();CHECK(job->cancelled&&!a.lesson&&!a.keys&&!a.historySize);a.onDestroy();lv_obj_delete(root);advance();CHECK(fake::handlers.empty()&&!fake::keys&&!fake::workers&&p::idle());CHECK(timers()==baseTimers);
  std::fprintf(stderr,"cycle=%u free=%zu\n",cycle,memory());
  if(cycle>=10)CHECK(memory()>=baseline[cycle%5]);baseline[cycle%5]=memory();
 }
 lv_indev_delete(pointer);lv_display_delete(display);lv_deinit();
}
int main(){models();gameModels();store();uiTest();std::printf("%u checks passed: 26 interactive object simulations / 78 missions; non-scripted valid solutions, semantic invariants, real touch, undo/error recovery, SD save integrity, five system palettes with one game palette, 20 bounded lifecycles, power pause and key ownership. Synthetic hardware only.\n",checks);}
