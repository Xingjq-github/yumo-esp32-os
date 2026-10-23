// Production Morse UI and model on real LVGL. Audio device and power are fakes.
#include "lvgl.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/morse_signal.hpp"
#include "core/game_audio.hpp"
#define private public
#include "apps/morse.hpp"
#undef private
// Audio worker is a test double in this UI replay.
namespace core { void WorkerWake::notify() {} }
static unsigned checks=0,home=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {bool screen=true;std::map<uint32_t,sys::EventHandler> handlers;uint32_t sub=0;}
namespace sys {
struct Power {static bool isScreenOn(){return fake::screen;}static bool sleepPending(){return false;}};
uint32_t EventBus::subscribe(EventType,EventHandler h){fake::handlers[++fake::sub]=h;return fake::sub;}
void EventBus::unsubscribe(uint32_t id){fake::handlers.erase(id);}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){++home;}
}
namespace core::game_audio {
std::shared_ptr<Session> start(Track t){return std::make_shared<Session>(t);}
}
static uint32_t esp_random(){return 123456789;}
#include "morse.inc"
static uint8_t framebuffer[240*240*2];
static lv_indev_t *touch;static bool pressed=false;static lv_point_t point{};
static void advance(unsigned ms=20){lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void sample(lv_obj_t *p,bool down){lv_obj_update_layout(p);lv_area_t a{};lv_obj_get_coords(p,&a);point={(a.x1+a.x2)/2,(a.y1+a.y2)/2};pressed=down;lv_tick_inc(20);lv_indev_read(touch);}
static void click(lv_obj_t *p){CHECK(p);lv_obj_send_event(p,LV_EVENT_CLICKED,nullptr);advance();}
static lv_obj_t *find(lv_obj_t *p,const char *s){
 for(unsigned i=0;i<lv_obj_get_child_count(p);++i){auto c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class)&&!std::strcmp(lv_label_get_text(c),s))return p;if(auto r=find(c,s))return r;}return nullptr;
}
static void key(apps::Morse &app,unsigned duration){sample(app.keyButton,true);if(!app.audio||!app.audio->morseKey){std::fprintf(stderr,"key point=%d,%d visible=%d state=%d holding=%d active=%p button=%p code=%s\n",point.x,point.y,app.visible,int(lv_obj_get_state(app.keyButton)),app.holding,(void*)lv_indev_get_active_obj(),(void*)app.keyButton,app.keyer.code.c_str());}CHECK(app.audio&&app.audio->morseKey);lv_tick_inc(duration-20);sample(app.keyButton,false);CHECK(!app.audio->morseKey);}
static void letter(apps::Morse &app,const char *code){for(auto p=code;*p;++p){key(app,*p=='.'?100:340);advance(80);}advance(620);}
static unsigned timers(){unsigned n=0;for(auto p=lv_timer_get_next(nullptr);p;p=lv_timer_get_next(p))++n;return n;}
static size_t freeMemory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void waiting(apps::Morse &app){
 CHECK(app.text.empty());CHECK(!std::strcmp(lv_label_get_text(app.output),"等待输入"));
 const auto *font=lv_obj_get_style_text_font(app.output,LV_PART_MAIN);
 for(uint32_t codepoint:{0x7B49u,0x5F85u,0x8F93u,0x5165u}){
  lv_font_glyph_dsc_t glyph{};
  CHECK(lv_font_get_glyph_dsc(font,&glyph,codepoint,0));
  CHECK(!glyph.is_placeholder&&glyph.box_w&&glyph.box_h);
 }
}
static void capture(const char *name){
 advance();lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");
 for(size_t i=0;i<sizeof(framebuffer);i+=2){const unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={static_cast<unsigned char>((c>>11)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
static void models(){
 using namespace apps::morse;
 std::set<std::string> codes;std::set<char> chars;
 for(auto &e:Entries){CHECK(codes.insert(e.code).second);CHECK(chars.insert(e.symbol).second);CHECK(decode(e.code)==e.symbol);for(size_t n=1;n<=std::strlen(e.code);++n)CHECK(prefix(std::string_view(e.code,n)));}
 CHECK(decode("....")=='H'&&decode(".")=='E'&&decode(".-..")=='L'&&decode("---")=='O');
 CHECK(decode("")==0&&decode("........")==0&&decode("123")==0);
 std::string text;unsigned bad=0;convert("  .... . .-.. .-.. ---  .-- --- .-. .-.. -..  ........  ",[&](Token t){if(!t.symbol)++bad;else text+=t.symbol;});
 CHECK(text=="HELLO WORLD "&&bad==1);
 Keyer k;k.press(100);CHECK(k.release(200));CHECK(!k.poll(799));CHECK(k.poll(800)&&k.value()=='E');CHECK(!k.poll(801));CHECK(!k.word(1599));CHECK(k.word(1600)&&!k.word(1601));
 k.press(1700);CHECK(k.code.empty());CHECK(k.release(1940)&&k.code=="-");
 k.reset();k.press(0xfffffff0u);CHECK(k.release(100));CHECK(k.poll(700)&&k.value()=='E');
 k.reset();k.press(1);CHECK(!k.release(10)&&k.code.empty());
 for(int n=0;n<9;++n){k.press(100+n*120);k.release(180+n*120);}CHECK(k.overflow&&k.value()==0&&k.code.size()==8);
 Practice p;p.start(0,false,1);CHECK(p.count==0);p.move(1);
 for(unsigned group=1;group<=7;++group){p.start(group,false,1);CHECK(p.count>0);std::set<char> seen;for(unsigned i=0;i<p.count;++i){CHECK(p.entry().group&group);seen.insert(p.entry().symbol);p.move(1);}CHECK(seen.size()==p.count);}
 p.start(7,true,123);for(int round=0;round<12;++round){std::set<char> seen;for(unsigned i=0;i<p.count;++i){seen.insert(p.entry().symbol);char prev=p.entry().symbol;p.move(1);CHECK(p.entry().symbol!=prev);}CHECK(seen.size()==Count);}
 using core::morse_audio::Signal;using core::morse_audio::encode;
 CHECK(encode("")==0&&encode("invalid")==0&&encode(".........")==0);
 for(auto &e:Entries){Signal s;s.play(encode(e.code));unsigned tone=0,steps=0;while(s.active()){const int sample=s.sample(false);CHECK(std::abs(sample)<=4096);if(s.length&&s.tone)++tone;CHECK(++steps<100000);}unsigned units=0;for(const char *c=e.code;*c;++c)units+=*c=='-'?3:1;CHECK(tone==units*Signal::Unit);CHECK(steps==(units+std::strlen(e.code))*Signal::Unit+1);}
 Signal s;for(unsigned i=0;i<16000;++i)s.sample(true);CHECK(s.active());s.clear();for(unsigned i=0;i<80;++i)s.sample(false);CHECK(!s.active());
 core::game_audio::Session session(core::game_audio::Track::Morse);core::game_audio::Session::MorseCommand cmd;
 for(unsigned round=0;round<128;++round){
  for(unsigned i=0;i<16;++i)CHECK(session.pushMorse(round*16+i+1));CHECK(!session.pushMorse(999));
  for(unsigned i=0;i<16;++i){CHECK(session.popMorse(cmd));CHECK(cmd.code==round*16+i+1&&cmd.epoch==session.morseReset);}CHECK(!session.popMorse(cmd));
 }
 CHECK(session.pushMorse(encode(".-")));session.morseKey=true;session.morseReady=true;session.pause();
 CHECK(!session.morseKey&&!session.morseReady&&session.popMorse(cmd)&&cmd.epoch!=session.morseReset);
 CHECK(session.pushMorse(encode("..."))&&session.popMorse(cmd)&&cmd.epoch==session.morseReset);
}
int main(){
 models();lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *d){d->point=point;d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 ui::theme::init(10);advance();const auto baseTimers=timers();size_t baseline=0;
 for(int cycle=0;cycle<31;++cycle){
  auto root=lv_obj_create(lv_screen_active());lv_obj_set_pos(root,0,0);lv_obj_set_size(root,240,240);apps::Morse app;app.onCreate(root);app.onShow();advance();
  if(!cycle)capture("idle.ppm");waiting(app);
  letter(app,".");CHECK(app.text=="E"&&lv_obj_get_style_text_font(app.output,LV_PART_MAIN)==&lv_font_montserrat_18);
  click(find(root,LV_SYMBOL_BACKSPACE));waiting(app);
  letter(app,".-");CHECK(app.text=="A");click(find(root,LV_SYMBOL_TRASH));waiting(app);
  click(app.tabs[1]);click(app.tabs[0]);waiting(app);
  CHECK(!app.allowsQuickPanel());letter(app,"...");CHECK(app.text=="S");letter(app,"---");letter(app,"...");CHECK(app.text=="SOS");
  advance(1500);CHECK(app.text=="SOS ");advance(1500);CHECK(app.text=="SOS ");
  if(!cycle)capture("live.ppm");
  sample(app.keyButton,true);auto job=app.audio;fake::screen=false;for(auto &h:fake::handlers)h.second(sys::Event{sys::EventType::ScreenOff});CHECK(!job->morseKey&&!job->morseReady);sample(app.keyButton,false);advance(2000);CHECK(app.text=="SOS ");fake::screen=true;advance();
  sample(app.keyButton,true);lv_obj_send_event(app.keyButton,LV_EVENT_PRESS_LOST,nullptr);CHECK(!app.audio->morseKey&&app.keyer.code.empty());sample(app.keyButton,false);
  sample(app.keyButton,true);advance(4100);CHECK(!app.audio->morseKey&&app.keyer.code.empty());sample(app.keyButton,false);
  click(app.tabs[1]);CHECK(app.mode==apps::Morse::Mode::Manual);CHECK(!app.allowsQuickPanel());
  click(find(root,"·"));click(find(root,"−"));click(find(root,"空格"));
  for(int i=0;i<8;++i)click(find(root,"·"));click(find(root,"转换"));
  CHECK(app.manual==".- ........");CHECK(lv_obj_get_child_count(app.output)==2);
  CHECK(!std::strcmp(lv_label_get_text(lv_obj_get_child(app.output,0)),"A"));
  CHECK(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(app.output,1),LV_PART_MAIN),lv_color_hex(0xFF717D)));
  if(!cycle)capture("manual.ppm");
  click(find(root,LV_SYMBOL_BACKSPACE));CHECK(app.manual==".- .......");
  click(app.tabs[0]);CHECK(app.text=="SOS ");click(app.tabs[1]);CHECK(app.manual==".- .......");
  app.manual.assign(160,'.');click(find(root,"·"));CHECK(app.manual.size()==160);click(find(root,LV_SYMBOL_TRASH));CHECK(app.manual.empty());
  click(app.tabs[2]);CHECK(app.allowsQuickPanel());click(app.cards[0]);CHECK(app.selected==0);const auto queued=app.audio->morseHead.load();click(app.cards[0]);CHECK(app.audio->morseHead==queued+1&&app.audio->morseQueue[queued%16].code==core::morse_audio::encode(".-"));
  click(app.cards[1]);CHECK(app.selected==1&&!lv_obj_has_state(app.cards[0],LV_STATE_CHECKED)&&lv_obj_has_state(app.cards[1],LV_STATE_CHECKED));
  if(!cycle)capture("dictionary.ppm");
  lv_obj_scroll_to_y(app.body,LV_COORD_MAX,LV_ANIM_OFF);advance();lv_area_t card{};lv_obj_get_coords(app.cards.back(),&card);CHECK(card.y2<=239&&card.y1>=70);
  click(app.tabs[3]);CHECK(app.allowsQuickPanel());click(find(root,"字母"));CHECK(app.groups==0&&lv_obj_has_state(find(root,"开始练习"),LV_STATE_DISABLED));
  click(find(root,"字母"));if(!cycle)capture("practice-menu.ppm");click(find(root,"开始练习"));CHECK(app.training&&!app.allowsQuickPanel()&&app.practice.entry().symbol=='A');
  letter(app,".-");CHECK(lv_color_eq(lv_obj_get_style_text_color(app.target,LV_PART_MAIN),lv_color_hex(0x5AFFAA)));if(!cycle)capture("practice-correct.ppm");
  letter(app,".");CHECK(lv_color_eq(lv_obj_get_style_text_color(app.target,LV_PART_MAIN),lv_color_hex(0xFF717D)));CHECK(std::strstr(lv_label_get_text(app.codeLabel)," / "));if(!cycle)capture("practice-error.ppm");
  letter(app,".-");CHECK(lv_color_eq(lv_obj_get_style_text_color(app.target,LV_PART_MAIN),lv_color_hex(0x5AFFAA)));
  click(find(root,">"));CHECK(app.practice.entry().symbol=='B');click(find(root,"<"));CHECK(app.practice.entry().symbol=='A');
  CHECK(app.onBack());advance();CHECK(!app.training&&app.allowsQuickPanel());CHECK(!app.onBack());
  job=app.audio;app.onHide();CHECK(job->stop&&!job->morseKey&&!job->morseReady);app.onDestroy();CHECK(fake::handlers.empty());lv_obj_delete(root);advance();CHECK(timers()==baseTimers);
  if(!cycle)baseline=freeMemory();else CHECK(freeMemory()>=baseline);
 }
 lv_indev_delete(touch);lv_display_delete(display);lv_deinit();
 std::printf("%u checks; 31 Morse UI lifecycles, key timing/wrap/overflow, all 54 codes, audio timing, random coverage, screen/exit cancellation, invalid red, repeated practice and bounded LVGL memory passed.\n",checks);
}
