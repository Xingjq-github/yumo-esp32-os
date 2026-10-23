#include "morse.hpp"
#include "core/event_bus.hpp"
#include "core/morse_signal.hpp"
#include "core/power_mgr.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "esp_random.h"
#include <cstdio>
#include <cstring>

namespace apps {
namespace {
constexpr uint32_t Black=0x030B08,Panel=0x0B2118,Green=0x5AFFAA,Dim=0x7AAA91,Edge=0x21543C,Red=0xFF717D;
enum Action {Back=1,Delete,Clear,Tab=10,Dot=20,Dash,Space,Convert,Group=30,Ordered=40,Shuffled,Start,Previous,Next,Card=100};
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,bool scroll=false) {
 auto p=lv_obj_create(parent);lv_obj_remove_style_all(p);lv_obj_set_pos(p,x,y);lv_obj_set_size(p,w,h);
 lv_obj_set_style_bg_color(p,lv_color_hex(Black),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
 lv_obj_set_clickable(p,false);lv_obj_set_scroll_dir(p,LV_DIR_VER);
 if(!scroll)lv_obj_set_scrollable(p,false);
 lv_obj_set_style_bg_color(p,lv_color_hex(Green),LV_PART_SCROLLBAR);
 lv_obj_set_style_width(p,2,LV_PART_SCROLLBAR);
 return p;
}
lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font=nullptr,uint32_t color=Green) {
 auto p=lv_label_create(parent);lv_obj_set_pos(p,x,y);lv_obj_set_width(p,w);
 lv_obj_set_style_text_font(p,font?font:ui::theme::fontMenu(),0);
 lv_obj_set_style_text_color(p,lv_color_hex(color),0);lv_label_set_text(p,text);
 lv_obj_set_clickable(p,false);return p;
}
void set(lv_obj_t *p,const char *text){if(p&&std::strcmp(lv_label_get_text(p),text))lv_label_set_text(p,text);}
void liveText(lv_obj_t *output,const std::string &text) {
 // The idle prompt is Chinese; decoded Morse remains in the larger Latin face.
 const auto *font=text.empty()?ui::theme::fontMenu():&lv_font_montserrat_18;
 if(lv_obj_get_style_text_font(output,LV_PART_MAIN)!=font)lv_obj_set_style_text_font(output,font,0);
 set(output,text.empty()?"等待输入":text.c_str());
}
void centerText(lv_obj_t *p){lv_obj_set_style_text_align(p,LV_TEXT_ALIGN_CENTER,0);}
lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *owner,int action,const lv_font_t *font=nullptr) {
 auto p=lv_button_create(parent);lv_obj_remove_style_all(p);lv_obj_set_pos(p,x,y);lv_obj_set_size(p,w,h);
 lv_obj_set_scrollable(p,false);lv_obj_set_press_lock(p,false);
 lv_obj_set_style_radius(p,5,0);lv_obj_set_style_border_width(p,1,0);lv_obj_set_style_border_color(p,lv_color_hex(Edge),0);
 lv_obj_set_style_bg_color(p,lv_color_hex(Panel),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
 lv_obj_set_style_text_color(p,lv_color_hex(Green),0);
 for(auto state:{LV_STATE_PRESSED,LV_STATE_CHECKED}) {
  lv_obj_set_style_bg_color(p,lv_color_hex(Green),state);lv_obj_set_style_text_color(p,lv_color_hex(Black),state);
 }
 lv_obj_set_style_bg_color(p,lv_color_hex(Black),LV_STATE_DISABLED);
 lv_obj_set_style_text_color(p,lv_color_hex(Dim),LV_STATE_DISABLED);
 auto l=lv_label_create(p);lv_obj_set_style_text_font(l,font?font:ui::theme::fontMenu(),0);
 lv_label_set_text(l,text);lv_obj_center(l);lv_obj_set_clickable(l,false);
 lv_obj_set_user_data(p,owner);if(cb)lv_obj_add_event_cb(p,cb,LV_EVENT_CLICKED,reinterpret_cast<void *>(intptr_t(action)));
 return p;
}
std::string marks(std::string_view code) {
 std::string s;s.reserve(code.size()*3);for(char c:code)s+=c=='.'?"·":c=='-'?"−":c==' '?" ":"?";return s;
}
void checked(lv_obj_t *p,bool selected){if(selected)lv_obj_add_state(p,LV_STATE_CHECKED);else lv_obj_remove_state(p,LV_STATE_CHECKED);}
}
void Morse::onCreate(lv_obj_t *parent) {
 m_root=parent;lv_obj_remove_style_all(parent);lv_obj_set_scrollable(parent,false);
 lv_obj_set_pos(parent,0,0);lv_obj_set_size(parent,240,240);
 lv_obj_set_style_bg_color(parent,lv_color_hex(Black),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
 text.reserve(morse::MaxText);manual.reserve(morse::MaxInput);keyer.code.reserve(8);
 screenSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &){cancelKey();});
 build();timer=lv_timer_create([](lv_timer_t *t){static_cast<Morse *>(lv_timer_get_user_data(t))->poll();},20,this);
}
void Morse::onShow(){visible=true;if(timer)lv_timer_resume(timer);sound();}
void Morse::onHide(){visible=false;cancelKey();if(audio){audio->requestStop();audio.reset();}if(timer)lv_timer_pause(timer);}
void Morse::onDestroy(){onHide();sys::EventBus::unsubscribe(screenSub);screenSub=0;if(timer)lv_timer_delete(timer);timer=nullptr;}
bool Morse::onBack(){if(mode==Mode::Practice&&training){cancelKey();training=false;rebuild=true;return true;}return false;}
void Morse::cancelKey() {
 holding=false;keyer.reset();if(audio)audio->pause();
 if(keyButton)lv_obj_remove_state(keyButton,LV_STATE_PRESSED);
}
void Morse::sound(uint32_t pattern) {
 if(!visible||!sys::Power::isScreenOn()||sys::Power::sleepPending())return;
 if(audio&&audio->failed.load()){audio->requestStop();audio.reset();}
 if(!audio)audio=core::game_audio::start(core::game_audio::Track::Morse);
 if(audio){audio->setMorseReady(mode!=Mode::Dictionary&&(mode!=Mode::Practice||training));if(pattern&&!audio->pushMorse(pattern)&&hint)set(hint,"输入已保留，试听稍后重试");}
 else if(hint)set(hint,"声音暂不可用，可继续输入");
}
void Morse::build() {
 cancelKey();lv_obj_clean(m_root);body=codeLabel=preview=output=hint=target=feedback=keyButton=nullptr;cards.fill(nullptr);
 button(m_root,LV_SYMBOL_LEFT,0,0,34,33,tap,this,Back,&lv_font_montserrat_18);
 label(m_root,"摩斯电码",41,7,112);
 button(m_root,LV_SYMBOL_BACKSPACE,163,0,37,33,tap,this,Delete,&lv_font_montserrat_18);
 button(m_root,LV_SYMBOL_TRASH,202,0,38,33,tap,this,Clear,&lv_font_montserrat_18);
 const char *names[]={"模拟","按键","字典","练习"};
 for(int i=0;i<4;++i){tabs[i]=button(m_root,names[i],i*60,35,59,33,tap,this,Tab+i);checked(tabs[i],i==int(mode));}
 body=box(m_root,0,70,240,170,mode==Mode::Dictionary);
 switch(mode){case Mode::Live:buildLive();break;case Mode::Manual:buildManual();break;case Mode::Dictionary:buildDictionary();break;case Mode::Practice:buildPractice();break;}
 sound();
}
void Morse::buildLive() {
 hint=label(body,"短按为点，长按为划，停顿转字",4,0,232,nullptr,Dim);
 codeLabel=label(body,"",7,26,170);lv_obj_set_style_text_letter_space(codeLabel,2,0);
 preview=label(body,"",182,20,52,&lv_font_montserrat_24);centerText(preview);
 auto area=box(body,4,55,232,59,true);output=label(area,"",3,1,224);liveText(output,text);
 keyButton=button(body,"按住输入",2,119,236,51,nullptr,this,0);
 lv_obj_add_event_cb(keyButton,keyEvent,LV_EVENT_ALL,this);
}
void Morse::buildManual() {
 hint=label(body,"空格分字，两个空格分词",4,0,232,nullptr,Dim);
 auto raw=box(body,4,22,232,42,true);codeLabel=label(raw,marks(manual).c_str(),2,0,226);
 output=box(body,4,66,232,49,true);lv_obj_set_flex_flow(output,LV_FLEX_FLOW_ROW_WRAP);
 lv_obj_set_style_pad_column(output,4,0);lv_obj_set_style_pad_row(output,2,0);
 label(output,"输入后点击转换",0,0,220,nullptr,Dim);
 button(body,"·",0,119,59,51,tap,this,Dot);
 button(body,"−",60,119,59,51,tap,this,Dash);
 button(body,"空格",120,119,59,51,tap,this,Space);
 button(body,"转换",180,119,60,51,tap,this,Convert);
 // Geometric key caps are legible without a large Unicode font asset.
 for(int i=0;i<2;++i){auto p=lv_obj_get_child(body,3+i);auto l=lv_obj_get_child(p,0);lv_obj_set_hidden(l,true);
  auto cap=box(p,0,0,i?23:9,i?4:9);lv_obj_set_style_bg_color(cap,lv_color_hex(Green),0);
  lv_obj_set_style_border_width(cap,1,0);lv_obj_set_style_border_color(cap,lv_color_hex(Black),0);
  lv_obj_set_style_radius(cap,i?1:LV_RADIUS_CIRCLE,0);lv_obj_center(cap);
 }
}
void Morse::buildDictionary() {
 label(body,"再次点击选中项，可试听电码",4,0,232,nullptr,Dim);
 int y=25;unsigned previous=0,col=0;
 for(size_t i=0;i<morse::Count;++i) {
  const auto &entry=morse::Entries[i];
  if(entry.group!=previous){if(col){y+=55;col=0;}label(body,entry.group==1?"字母 A-Z":entry.group==2?"数字 0-9":"符号 / 常用扩展",4,y,232);y+=23;previous=entry.group;}
  auto p=button(body,"",int(col)*80,y,78,52,tap,this,Card+int(i));cards[i]=p;
  auto sym=lv_obj_get_child(p,0);char s[]={entry.symbol,0};lv_label_set_text(sym,s);lv_obj_set_style_text_font(sym,&lv_font_montserrat_24,0);lv_obj_align(sym,LV_ALIGN_TOP_MID,0,1);
  auto code=lv_label_create(p);lv_label_set_text(code,marks(entry.code).c_str());lv_obj_set_style_text_font(code,ui::theme::fontMenu(),0);lv_obj_align(code,LV_ALIGN_BOTTOM_MID,0,-3);
  checked(p,selected==int(i));if(++col==3){col=0;y+=55;}
 }
}
void Morse::buildPractice() {
 if(!training) {
  hint=label(body,"选择练习内容，可多选",4,1,232,nullptr,Dim);
  const char *names[]={"字母","数字","符号"};
  for(int i=0;i<3;++i){auto b=button(body,names[i],i*80,27,78,35,tap,this,Group+i);checked(b,groups&(1u<<i));}
  auto a=button(body,"按顺序",0,70,118,37,tap,this,Ordered);checked(a,!random);
  auto b=button(body,"随机",120,70,120,37,tap,this,Shuffled);checked(b,random);
  auto start=button(body,"开始练习",2,119,236,51,tap,this,Start);if(!groups)lv_obj_add_state(start,LV_STATE_DISABLED);
  return;
 }
 char pos[40];std::snprintf(pos,sizeof(pos),"%s  %u / %u",random?"随机":"顺序",practice.position+1,practice.count);
 hint=label(body,pos,4,0,232,nullptr,Dim);
 char s[]={practice.entry().symbol,0};target=label(body,s,0,18,240,&lv_font_montserrat_48);centerText(target);
 codeLabel=label(body,"",4,75,232);centerText(codeLabel);
 feedback=label(body,"短按点 / 长按划",4,97,232,nullptr,Dim);centerText(feedback);
 button(body,"<",0,119,59,51,tap,this,Previous,&lv_font_montserrat_24);
 keyButton=button(body,"输入",60,119,118,51,nullptr,this,0);lv_obj_add_event_cb(keyButton,keyEvent,LV_EVENT_ALL,this);
 button(body,">",180,119,60,51,tap,this,Next,&lv_font_montserrat_24);
}
void Morse::paintKey() {
 set(codeLabel,marks(keyer.code).c_str());
 if(mode==Mode::Live){char result[]={keyer.value()?keyer.value():'?',0};set(preview,keyer.code.empty()?"":result);lv_obj_set_style_text_color(preview,lv_color_hex(keyer.value()?Green:Red),0);}
 if(codeLabel)lv_obj_set_style_text_color(codeLabel,lv_color_hex(keyer.overflow||!morse::prefix(keyer.code)?Red:Green),0);
}
void Morse::commit() {
 const char result=keyer.value();paintKey();
 if(mode==Mode::Live) {
  if(text.size()<morse::MaxText){text+=result?result:'?';liveText(output,text);lv_obj_scroll_to_y(lv_obj_get_parent(output),LV_COORD_MAX,LV_ANIM_OFF);}
  else set(hint,"已达96字，请删除或清空");
  if(preview)lv_obj_set_style_text_color(preview,lv_color_hex(result?Green:Red),0);
 } else if(mode==Mode::Practice&&training) {
  const bool correct=result==practice.entry().symbol;
  lv_obj_set_style_text_color(target,lv_color_hex(correct?Green:Red),0);
  lv_obj_set_style_text_color(codeLabel,lv_color_hex(correct?Green:Red),0);
  if(correct){set(feedback,"正确，可再次输入或切换");lv_obj_set_style_text_color(feedback,lv_color_hex(Green),0);}
  else {
   // Put the correction beside the attempted sequence, not behind a dialog.
   auto attempt=marks(keyer.code)+"  /  "+marks(practice.entry().code);set(codeLabel,attempt.c_str());
   set(feedback,"错误 / 右侧为正确电码");lv_obj_set_style_text_color(feedback,lv_color_hex(Red),0);
  }
 }
}
void Morse::convert() {
 lv_obj_clean(output);bool bad=false;
 morse::convert(manual,[&](morse::Token token){
  std::string s;if(token.word)s=" / ";else if(token.symbol)s+=token.symbol;else{s="["+marks(token.code)+"]";bad=true;}
  auto l=label(output,s.c_str(),0,0,LV_SIZE_CONTENT,token.symbol?&lv_font_montserrat_18:ui::theme::fontMenu(),token.symbol?Green:Red);
  lv_obj_set_style_max_width(l,222,0);
 });
 if(manual.empty())label(output,"请先输入电码",0,0,220,nullptr,Dim);
 set(hint,bad?"红色电码无法转换，可删除修改":"空格分字，两个空格分词");
}
void Morse::select(size_t index) {
 if(index>=morse::Count)return;
 if(selected==int(index)){sound(core::morse_audio::encode(morse::Entries[index].code));return;}
 if(selected>=0&&cards[selected])checked(cards[selected],false);
 selected=int(index);checked(cards[index],true);
 if(audio)audio->pause();
}
void Morse::reset(bool erase) {
 if(mode==Mode::Manual) {
  if(erase){if(!manual.empty())manual.pop_back();}else manual.clear();
  set(codeLabel,marks(manual).c_str());lv_obj_clean(output);set(hint,"空格分字，两个空格分词");
 } else if(mode==Mode::Live||training) {
  if(erase&&!keyer.finished&&!keyer.code.empty()) {
   keyer.down=false;holding=false;if(audio)audio->pause();keyer.code.pop_back();keyer.overflow=false;keyer.upAt=lv_tick_get();paintKey();
  } else {
   cancelKey();if(mode==Mode::Live){if(erase){if(!text.empty())text.pop_back();}else text.clear();liveText(output,text);}
   paintKey();
  }
  if(training){set(feedback,"短按点 / 长按划");lv_obj_set_style_text_color(target,lv_color_hex(Green),0);}
 }
}
void Morse::keyEvent(lv_event_t *e) {
 auto self=static_cast<Morse *>(lv_event_get_user_data(e));auto type=lv_event_get_code(e);
 if(!self->visible||!sys::Power::isScreenOn()||sys::Power::sleepPending())return;
 const auto now=lv_tick_get();
 if(type==LV_EVENT_PRESSED) {
  if(self->keyer.poll(now))self->commit();
  if(self->mode==Mode::Live&&self->keyer.word(now)&&!self->text.empty()&&self->text.back()!=' '&&self->text.size()<morse::MaxText){self->text+=' ';liveText(self->output,self->text);}
  self->keyer.press(now);self->holding=true;self->sound();if(self->audio)self->audio->setMorseKey(true);
  self->paintKey();if(self->feedback)set(self->feedback,"松开后停顿，自动判定");
 } else if(type==LV_EVENT_RELEASED) {
  if(self->audio)self->audio->setMorseKey(false);
  self->holding=false;if(self->keyer.release(now))self->paintKey();
 } else if(type==LV_EVENT_PRESS_LOST) {self->cancelKey();self->paintKey();}
}
void Morse::tap(lv_event_t *e) {
 auto self=static_cast<Morse *>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
 const int action=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
 if(!self->visible||!sys::Power::isScreenOn()||sys::Power::sleepPending())return;
 if(action==Back){if(!self->onBack())ui::WindowManager::instance().goHome();return;}
 if(action>=Card){self->select(action-Card);return;}
 if(action>=Tab&&action<Tab+4){if(int(self->mode)!=action-Tab)self->pendingMode=action-Tab;return;}
 if(action==Delete||action==Clear){self->reset(action==Delete);return;}
 if(action==Dot||action==Dash||action==Space) {
  if(self->manual.size()>=morse::MaxInput){set(self->hint,"已达160格，请删除或清空");return;}
  const char c=action==Dot?'.':action==Dash?'-':' ';self->manual+=c;set(self->codeLabel,marks(self->manual).c_str());
  lv_obj_scroll_to_y(lv_obj_get_parent(self->codeLabel),LV_COORD_MAX,LV_ANIM_OFF);
  lv_obj_clean(self->output);set(self->hint,"空格分字，两个空格分词");if(c!=' ')self->sound(core::morse_audio::encode(std::string_view(&c,1)));return;
 }
 if(action==Convert){self->convert();return;}
 if(action>=Group&&action<Group+3){self->groups^=1u<<(action-Group);self->rebuild=true;}
 else if(action==Ordered||action==Shuffled){self->random=action==Shuffled;self->rebuild=true;}
 else if(action==Start&&self->groups){self->practice.start(self->groups,self->random,esp_random());self->training=true;self->rebuild=true;}
 else if(action==Previous||action==Next)self->pendingStep=action==Previous?-1:1;
}
void Morse::poll() {
 if(!visible)return;
 if(pendingMode>=0){mode=Mode(pendingMode);pendingMode=-1;rebuild=true;}
 if(pendingStep){practice.move(pendingStep);pendingStep=0;rebuild=true;}
 if(rebuild){rebuild=false;build();}
 if(!sys::Power::isScreenOn()||sys::Power::sleepPending()){if(holding)cancelKey();return;}
 if(audio)audio->setMorseReady(mode!=Mode::Dictionary&&(mode!=Mode::Practice||training));
 const auto now=lv_tick_get();
 if(holding&&uint32_t(now-keyer.downAt)>=morse::Keyer::MaxHoldMs){cancelKey();paintKey();set(hint,"按住过久已取消，请重新输入");}
 if(keyer.poll(now))commit();
 if(mode==Mode::Live&&keyer.word(now)&&!text.empty()&&text.back()!=' '&&text.size()<morse::MaxText){text+=' ';liveText(output,text);}
}
}
