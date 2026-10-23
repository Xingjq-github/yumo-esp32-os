// Real LVGL and production music App; only media/system services are doubles.
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
#include "apps/mp3_music.hpp"
#undef private
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {int volume=50,starts=0;bool busy=false;std::string last;std::shared_ptr<core::mp3::Listing> listing;std::shared_ptr<core::mp3::Player> player;}
namespace sys {
struct Settings {static int volume(){return fake::volume;}static std::string mp3Track(){return fake::last;}};
struct SystemService {static void setVolume(int v){fake::volume=v;}};
}
namespace core::mp3 {
std::shared_ptr<Listing> scan(){fake::listing=std::make_shared<Listing>();return fake::listing;}
std::shared_ptr<Player> start(const std::string &path){if(fake::busy)return {};fake::player=std::make_shared<Player>();fake::player->path=path;++fake::starts;return fake::player;}
bool idle(){return !fake::busy;}
}
namespace ui {WindowManager &WindowManager::instance(){static WindowManager w;return w;}void WindowManager::goBack(){}void TextInput::bindField(lv_obj_t *){}}
#include "app.inc"
static uint8_t framebuffer[240*240*2];
static lv_indev_t *touch=nullptr;static bool down=false;static lv_point_t point{};
static void tap(int x,int y){point={x,y};down=true;lv_tick_inc(20);lv_indev_read(touch);down=false;lv_tick_inc(20);lv_indev_read(touch);}
static void advance(){lv_tick_inc(100);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static unsigned timers(){unsigned n=0;for(auto p=lv_timer_get_next(nullptr);p;p=lv_timer_get_next(p))++n;return n;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void bounds(lv_obj_t *root){
 lv_obj_update_layout(root);for(unsigned i=0;i<lv_obj_get_child_count(root);++i){auto p=lv_obj_get_child(root,i);lv_area_t a{};lv_obj_get_coords(p,&a);if(a.x1<0||a.x2>=240||a.y1<0||a.y2>=240)std::fprintf(stderr,"child %u bounds %d,%d %d,%d\n",i,a.x1,a.y1,a.x2,a.y2);CHECK(a.x1>=0&&a.x2<240&&a.y1>=0&&a.y2<240);if(lv_obj_check_type(p,&lv_button_class))CHECK(a.y2-a.y1+1>=36);}
}
static void capture(const char *name){advance();lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(size_t i=0;i<sizeof(framebuffer);i+=2){unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}
int main(){
 lv_init();auto d=lv_display_create(240,240);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,[](lv_indev_t*,lv_indev_data_t *data){data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;data->point=point;});
 const auto baseTimers=timers();
 for(int theme:{4,5,6,8,10}){
  ui::theme::init(theme);size_t baseline=0;
  for(int cycle=0;cycle<8;++cycle){
   fake::last=cycle%2?"/sdcard/MP3/missing.mp3":"/sdcard/MP3/7.mp3";
   const auto starts=fake::starts;
   fake::busy=false;auto root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,0);lv_obj_set_size(root,240,240);apps::Mp3Music app;app.onCreate(root);advance();CHECK(app.listing&&!app.player&&app.playback);bounds(app.pageRoot);
   auto listing=fake::listing;for(int i=0;i<9;++i)listing->entries.push_back({"/sdcard/MP3/"+std::to_string(i)+".mp3",i?"星河里的晚风":"雨墨 · 午后散步"});
   listing->done=true;app.poll();advance();CHECK(app.entries.size()==9&&!app.listing);bounds(app.pageRoot);
   CHECK(app.selected==(cycle%2?0:7)&&app.playback&&!app.player&&fake::starts==starts);
   CHECK(!std::strcmp(lv_label_get_text(app.position),"00:00"));
   CHECK(!std::strcmp(lv_label_get_text(app.track),app.entries[app.selected].title.c_str()));
   if(!cycle&&theme==6)capture("entry.ppm");
   tap(220,161);advance();CHECK(!app.playback);CHECK(app.onBack()&&app.playback);advance();tap(220,161);advance();CHECK(!app.playback);
   if(!cycle&&theme==6)capture("list.ppm");
   if(app.page)app.action(3);
   app.action(4);CHECK(app.page==1);app.action(4);CHECK(app.page==2);app.action(3);app.action(3);app.action(100);app.poll();
   auto p=fake::player;CHECK(p&&p->path=="/sdcard/MP3/0.mp3");
   p->rate=44100;p->bitrate=320;p->duration=184000;p->position=42700;p->level=60;p->state=core::mp3::State::Playing;
   p->lyrics={{0,"窗外的风轻轻吹过"},{40000,"让音乐陪你走一走"},{80000,"把时光留在这一刻"}};p->metadataReady=true;app.poll();advance();bounds(app.pageRoot);
   CHECK(!lv_obj_has_state(app.playButton,LV_STATE_DISABLED));CHECK(std::strstr(lv_label_get_text(app.position),"00:42"));
   if(!cycle){char n[40];std::snprintf(n,sizeof(n),"player-%d.ppm",theme);capture(n);}
   CHECK(!app.onBack());tap(220,161);advance();CHECK(!p->cancel&&!app.playback);app.action(100);CHECK(app.player==p);app.poll();
   app.action(6);CHECK(!p->play);p->state=core::mp3::State::Paused;app.poll();CHECK(!std::strcmp(lv_label_get_text(lv_obj_get_child(app.playButton,0)),LV_SYMBOL_PLAY));app.action(6);CHECK(p->play);p->state=core::mp3::State::Playing;
   bounds(app.pageRoot);CHECK(!std::strcmp(lv_label_get_text(app.lyric[1]),"让音乐陪你走一走"));
   if(!cycle&&theme==6)capture("lyrics.ppm");
   lv_slider_set_value(app.slider,500,LV_ANIM_OFF);lv_obj_send_event(app.slider,LV_EVENT_RELEASED,nullptr);CHECK(p->seekMs==92000);
   app.action(12);CHECK(app.repeat);
   p->state=core::mp3::State::Ended;p->done=true;app.poll();app.poll();CHECK(app.selected==0);p=fake::player;
   p->error="扬声器输出失败";p->state=core::mp3::State::Failed;p->done=true;app.poll();CHECK(lv_obj_is_hidden(app.lyric[2]));CHECK(std::strstr(lv_label_get_text(app.status),"扬声器"));
   app.action(6);app.poll();CHECK(app.player!=p);
   p=app.player;p->metadataReady=true;p->state=core::mp3::State::Playing;app.poll();
   CHECK(!std::strcmp(lv_label_get_text(app.track),app.entries[app.selected].title.c_str()));
   // Long lyrics/title must remain readable and never overlap transport.
   app.focusText("这是一段用于检查长歌词显示的文本不能遮挡进度和播放按钮且可以滚动查看完整内容");advance();
   CHECK(lv_label_get_long_mode(app.track)==LV_LABEL_LONG_MODE_SCROLL);
   CHECK(lv_obj_get_y(app.lyricFocus)+lv_obj_get_height(app.lyricFocus)<lv_obj_get_y(app.slider));
   app.focusText("夜色里的风");advance();CHECK(lv_label_get_long_mode(app.track)==LV_LABEL_LONG_MODE_WRAP);
   CHECK(lv_obj_get_style_text_font(app.track,LV_PART_MAIN)->line_height==28);
   if(!cycle&&theme==6)capture("no-lyrics.ppm");
   for(unsigned i=0;i<lv_obj_get_child_count(app.pageRoot);++i){auto o=lv_obj_get_child(app.pageRoot,i);if(lv_obj_check_type(o,&lv_button_class)){const auto id=int(intptr_t(lv_obj_get_user_data(o)));CHECK(id!=1&&id!=10&&id!=11);}}
   // A busy output fails visibly and remains retryable.
   fake::busy=true;app.choose((app.selected+1)%int(app.entries.size()));
   lv_tick_inc(6100);app.poll();CHECK(app.pending==-1&&!app.player);
   CHECK(std::strstr(lv_label_get_text(app.status),"音频忙"));
   app.poll();CHECK(std::strstr(lv_label_get_text(app.status),"音频忙"));
   fake::busy=false;app.action(6);app.poll();CHECK(app.player);
   p=app.player;app.onHide();CHECK(p->cancel);app.onDestroy();lv_obj_delete(root);fake::listing.reset();fake::player.reset();advance();CHECK(timers()==baseTimers);
   if(!cycle)baseline=memory();else CHECK(memory()>=baseline);
  }
 }
 for(const char *error:{"SD /MP3 中没有音乐","SD卡不可用"}) {
  auto root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);lv_obj_set_size(root,240,240);
  apps::Mp3Music app;const auto starts=fake::starts;app.onCreate(root);fake::listing->error=error;fake::listing->done=true;app.poll();advance();
  CHECK(app.playback&&app.selected==-1&&!std::strcmp(lv_label_get_text(app.track),error));
  CHECK(lv_obj_has_state(app.playButton,LV_STATE_DISABLED));tap(120,215);app.poll();CHECK(fake::starts==starts);
  capture("empty.ppm");tap(220,161);advance();CHECK(!app.playback);tap(220,18);CHECK(app.listing);CHECK(app.onBack());
  app.onHide();app.onDestroy();lv_obj_delete(root);fake::listing.reset();advance();CHECK(timers()==baseTimers);
 }
 lv_display_delete(d);lv_deinit();std::printf("%u checks; 40 real App/LVGL lifecycles across five themes, bounds, seek, automatic lyrics, speaker errors and release passed\n",checks);
}
