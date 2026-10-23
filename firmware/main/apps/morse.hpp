#pragma once
#include "ui/app.hpp"
#include "core/game_audio.hpp"
#include "morse_model.hpp"
#include <array>
#include <string>
namespace apps {
class Morse final:public ui::App {
public:
 const char *name()const override{return "摩斯电码";}
 const char *icon()const override{return LV_SYMBOL_BULLET LV_SYMBOL_MINUS;}
 bool handlesHorizontalGesture()const override{return true;}
 bool allowsQuickPanel()const override{return mode==Mode::Dictionary||(mode==Mode::Practice&&!training);}
 void onCreate(lv_obj_t *)override;
 void onShow()override;
 void onHide()override;
 void onDestroy()override;
 bool onBack()override;
private:
 enum class Mode {Live,Manual,Dictionary,Practice};
 Mode mode=Mode::Live;
 int pendingMode=-1,pendingStep=0,selected=-1;
 bool visible=false,training=false,random=false,rebuild=false,holding=false;
 unsigned groups=1;uint32_t screenSub=0;
 morse::Keyer keyer;morse::Practice practice;
 std::string text,manual;
 std::shared_ptr<core::game_audio::Session> audio;
 lv_timer_t *timer=nullptr;
 lv_obj_t *body=nullptr,*codeLabel=nullptr,*preview=nullptr,*output=nullptr,*hint=nullptr,*target=nullptr,*feedback=nullptr,*keyButton=nullptr;
 std::array<lv_obj_t *,4> tabs{};
 std::array<lv_obj_t *,morse::Count> cards{};
 void build();void buildLive();void buildManual();void buildDictionary();void buildPractice();
 void poll();void commit();void paintKey();void convert();void cancelKey();
 void reset(bool erase);void sound(uint32_t pattern=0);void select(size_t index);
 static void keyEvent(lv_event_t *);static void tap(lv_event_t *);
};
}
