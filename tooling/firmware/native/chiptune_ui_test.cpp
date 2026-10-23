// Actual App/LVGL with fake completed jobs. No audio device or SD access.
#include "lvgl.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/window_mgr.hpp"
#define private public
#include "apps/chiptune.hpp"
#undef private
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {int volume=50,starts=0;bool busy=false;std::shared_ptr<core::chip::Listing> listing;std::shared_ptr<core::chip::Player> player;}
namespace sys {
struct Settings {static int volume(){return fake::volume;}};
struct SystemService {static void setVolume(int v){fake::volume=v;}};
}
namespace core::chip {
void Player::wake(){}
std::shared_ptr<Listing> titles(const std::vector<Entry> &,size_t){return {};}
std::shared_ptr<Listing> scan(){fake::listing=std::make_shared<Listing>();return fake::listing;}
std::shared_ptr<Player> start(const std::string &path){if(fake::busy)return {};fake::player=std::make_shared<Player>();fake::player->path=path;++fake::starts;return fake::player;}
bool idle(){return !fake::busy;}
}
namespace ui {WindowManager &WindowManager::instance(){static WindowManager w;return w;}void WindowManager::goBack(){}void TextInput::bindField(lv_obj_t *){}}
#include "app.inc"
static uint8_t framebuffer[240*240*2];
static void advance(){lv_tick_inc(100);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static unsigned timers(){unsigned n=0;for(auto p=lv_timer_get_next(nullptr);p;p=lv_timer_get_next(p))++n;return n;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void bounds(lv_obj_t *root){
 lv_obj_update_layout(root);for(unsigned i=0;i<lv_obj_get_child_count(root);++i){auto p=lv_obj_get_child(root,i);lv_area_t a{};lv_obj_get_coords(p,&a);if(a.x1<0||a.x2>=240||a.y1<0||a.y2>=240)std::fprintf(stderr,"child %u bounds %d,%d %d,%d\n",i,a.x1,a.y1,a.x2,a.y2);CHECK(a.x1>=0&&a.x2<240&&a.y1>=0&&a.y2<240);if(lv_obj_check_type(p,&lv_button_class))CHECK(a.y2-a.y1+1>=36);}
}
static void capture(const char *name){advance();lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(size_t i=0;i<sizeof(framebuffer);i+=2){unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}
int main(){
 lv_init();auto d=lv_display_create(240,240);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 const auto baseTimers=timers();
 for(int theme:{4,5,6,8,10}){
  ui::theme::init(theme);size_t baseline=0;
  for(int cycle=0;cycle<12;++cycle){
   fake::busy=false;auto root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,0);lv_obj_set_size(root,240,240);apps::Chiptune app;app.onCreate(root);advance();CHECK(app.listing&&!app.player&&!app.playback);bounds(app.pageRoot);
   auto listing=fake::listing;for(int i=0;i<9;++i)listing->entries.push_back({"/sdcard/chiptune/"+std::to_string(i)+".chip.json","test.chip.json",i?"雨墨·像素冒险":"卡农·像素变奏",{}});
   listing->done=true;app.poll();advance();CHECK(app.entries.size()==9&&!app.listing);bounds(app.pageRoot);
   if(!cycle&&theme==10)capture("list.ppm");
   const int starts=fake::starts;app.action(4);CHECK(app.page==1);app.action(4);CHECK(app.page==2);app.action(3);app.action(3);CHECK(app.page==0);app.action(100);CHECK(app.playback&&app.selected==0&&fake::starts==starts+1);
   auto p=fake::player;CHECK(p&&p->path=="/sdcard/chiptune/0.chip.json");CHECK(app.onBack());CHECK(p->cancel&&!app.player&&!app.playback);
   app.action(100);p=fake::player;p->title="卡农·像素变奏";p->tempo=120;p->duration=4000;p->position=1700;p->state=core::chip::State::Playing;app.poll();advance();bounds(app.pageRoot);
   CHECK(!lv_obj_has_state(app.playButton,LV_STATE_DISABLED));CHECK(std::strstr(lv_label_get_text(app.position),"00:01"));CHECK(lv_obj_has_state(app.previous,LV_STATE_DISABLED));
   if(!cycle&&theme==10)capture("player.ppm");
   CHECK(app.onBack());CHECK(!p->cancel&&app.player==p&&!app.playback);const int same=fake::starts;app.action(100);CHECK(fake::starts==same&&app.player==p);
   app.action(6);CHECK(!p->play);p->state=core::chip::State::Paused;app.poll();CHECK(!std::strcmp(lv_label_get_text(lv_obj_get_child(app.playButton,0)),LV_SYMBOL_PLAY));app.action(6);CHECK(p->play);
   app.action(10);CHECK(p->loop&&lv_obj_has_state(app.loopButton,LV_STATE_CHECKED));app.action(10);CHECK(!p->loop);
   app.action(11);CHECK(p->rewind&&!p->play);p->rewind=false;p->state=core::chip::State::Stopped;app.poll();
   CHECK(app.onBack());app.action(100);CHECK(p->play);app.action(8);app.action(9);CHECK(fake::volume==50);
   app.action(7);CHECK(p->cancel&&app.selected==1);p=fake::player;p->error="JSON错误 行2 列4";p->state=core::chip::State::Failed;p->done=true;app.poll();CHECK(lv_obj_has_state(app.playButton,LV_STATE_DISABLED));CHECK(std::strstr(lv_label_get_text(app.status),"JSON"));
   app.onBack();app.refresh();listing=fake::listing;app.onHide();CHECK(listing->cancel);app.onDestroy();lv_obj_delete(root);fake::listing.reset();fake::player.reset();advance();CHECK(timers()==baseTimers);
   if(!cycle)baseline=memory();else CHECK(memory()>=baseline);
  }
 }
 lv_display_delete(d);lv_deinit();std::printf("%u checks; 60 real App/LVGL lifecycles across five themes, bounds, navigation, controls, errors and memory release passed\n",checks);
}
