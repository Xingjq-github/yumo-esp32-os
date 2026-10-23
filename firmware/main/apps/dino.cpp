#include "dino.hpp"
#include "dino_sprites.hpp"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "game_widgets.h"
#include "window_mgr.hpp"
#include "core/settings_store.hpp"
#include "core/event_bus.hpp"
#include "core/button_mgr.hpp"
#include "core/power_mgr.hpp"
#include "esp_random.h"
#include <algorithm>
#include <cstdio>
namespace apps {
namespace c=ui::compact;
namespace {
void noTouch(lv_obj_t *o){lv_obj_set_clickable(o,false);lv_obj_set_scrollable(o,false);}
lv_obj_t *pixel(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color){
    auto o=c::panel(parent,w,h);lv_obj_set_pos(o,x,y);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);noTouch(o);return o;
}
int wrapPosition(int initial,float offset,int span){
    int x=initial-int(offset);if(x<-120)x+=span;return x;
}
// Angular, two-tone pixel slopes are deliberately unlike the flat-bottomed
// clouds. Fixed child rectangles are made only when opening the app.
void peak(lv_obj_t *parent,int left,int top,int width,int height,int plateau,uint32_t face,uint32_t shade,bool snow=false){
    for(int y=0;y<height;y+=6){
        const int h=std::min(6,height-y),span=plateau+(width-plateau)*(y+h)/height;
        const int x=left+(width-span)/2;
        pixel(parent,x,top+y,span,h,snow&&y<12?0xEEF0E8:face);
        pixel(parent,x+span/2,top+y,span-span/2,h,snow&&y<12?0xD4DDDB:shade);
    }
}
}
void Dino::createScenery(){
    // Four parallax layers, all bounded pools, no frame-time object creation.
    for(unsigned i=0;i<3;++i){
        auto cloud=clouds[i]=c::panel(screen,70,22);lv_obj_set_style_bg_opa(cloud,LV_OPA_TRANSP,0);noTouch(cloud);
        if(i==0){ // Small cumulus, with a separate low lobe.
            pixel(cloud,0,12,48,6,0xDCE7ED);pixel(cloud,6,6,18,8,0xDCE7ED);
            pixel(cloud,12,1,10,5,0xDCE7ED);pixel(cloud,27,8,14,6,0xDCE7ED);
            pixel(cloud,7,18,34,2,0xC4D5DE);
        }else if(i==1){ // Thin, broken wind streaks.
            pixel(cloud,2,5,43,3,0xD1E0E8);pixel(cloud,18,10,46,3,0xD1E0E8);
            pixel(cloud,0,13,12,2,0xE1EAF0);pixel(cloud,48,3,15,2,0xE1EAF0);
        }else{ // Wide bank with unequal lobes and a shaded flat base.
            pixel(cloud,1,13,64,5,0xDCE7ED);pixel(cloud,6,8,25,6,0xDCE7ED);
            pixel(cloud,14,3,12,6,0xDCE7ED);pixel(cloud,36,6,19,8,0xDCE7ED);
            pixel(cloud,41,1,9,5,0xDCE7ED);pixel(cloud,8,18,50,2,0xC4D5DE);
        }
    }
    for(unsigned i=0;i<2;++i){
        auto ridge=ridges[i]=c::panel(screen,116,42);lv_obj_set_style_bg_opa(ridge,LV_OPA_TRANSP,0);noTouch(ridge);
        peak(ridge,0,i?0:12,74,i?42:30,12,0xE6EBE8,0xDEE5E2);
        peak(ridge,49,i?18:6,67,i?24:36,4,0xE6EBE8,0xDEE5E2);
    }
    for(unsigned i=0;i<3;++i){
        auto mountain=mountains[i]=c::panel(screen,104,72);lv_obj_set_style_bg_opa(mountain,LV_OPA_TRANSP,0);noTouch(mountain);
        if(i==0)peak(mountain,4,0,96,72,0,0xBCCDC5,0xA4B9AF,true);
        else if(i==1){peak(mountain,0,24,66,48,0,0xBECBC5,0xA8BBB2);peak(mountain,38,12,66,60,0,0xBDCEC5,0x9FB7AC);}
        else { // A low stratified mesa, distinct from the pointed/twin peaks.
            peak(mountain,3,24,98,48,38,0xD2CBB9,0xBDB59F);
            pixel(mountain,29,24,46,3,0xB2AA93);pixel(mountain,20,46,32,3,0xBEB69F);
            pixel(mountain,12,64,40,3,0xC4BBA5);
        }
    }
    for(unsigned i=0;i<9;++i)stones[i]=pixel(screen,0,0,i%3?4:7,i%3?1:2,0x969C9E);
    moveScenery();
}
void Dino::moveScenery(){
    for(unsigned i=0;i<3;++i){
        lv_obj_set_pos(clouds[i],wrapPosition(25+int(i)*160,cloudOffset,480),43+int(i%2)*28);
        lv_obj_set_pos(mountains[i],wrapPosition(-22+int(i)*150,mountainOffset,450),dino::Ground-72);
    }
    for(unsigned i=0;i<2;++i)lv_obj_set_pos(ridges[i],wrapPosition(76+int(i)*220,ridgeOffset,440),132);
    for(unsigned i=0;i<9;++i)lv_obj_set_pos(stones[i],wrapPosition(int(i)*39,stoneOffset,351),dino::Ground+5+int(i%3)*5);
}
void Dino::menuAction(lv_event_t *e){
    auto s=static_cast<Dino *>(lv_event_get_user_data(e));
    const auto action=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(action==0)s->resumePending=true;else if(action==1)s->restartPending=true;else s->exitPending=true;
}
void Dino::createMenu(){
    menu=game_overlay(screen);
    // A pixel sign above the runner and controls beside/below the playfield.
    // The actual mountains, ground and dinosaur remain visible while paused.
    auto sign=game_box(menu,48,44,180,78,lv_color_hex(0xF5F8F0),0);
    lv_obj_set_style_border_width(sign,2,0);lv_obj_set_style_border_color(sign,lv_color_hex(0x657969),0);
    pixel(menu,54,122,168,3,0xC3D0C5);
    menuText=game_text(menu,"小恐龙",58,52,160,lv_color_hex(0x344D3E));
    lv_obj_set_style_text_align(menuText,LV_TEXT_ALIGN_CENTER,0);
    menuDetail=game_text(menu,"触屏跳跃\n拾取道具加分",58,76,160,lv_color_hex(0x535353));
    lv_obj_set_style_text_align(menuDetail,LV_TEXT_ALIGN_CENTER,0);
    menuResume=game_button(menu,"开始",64,134,162,40,lv_color_hex(0x536F5E),lv_color_white(),lv_color_hex(0x344D3E),0,menuAction,this);
    auto restart=game_button(menu,"重来",10,197,106,36,lv_color_hex(0xF5F8F0),lv_color_hex(0x344D3E),lv_color_hex(0x657969),0,menuAction,this);
    lv_obj_set_user_data(restart,reinterpret_cast<void *>(uintptr_t(1)));
    auto exit=game_button(menu,"退出",124,197,106,36,lv_color_hex(0xF5F8F0),lv_color_hex(0x344D3E),lv_color_hex(0x657969),0,menuAction,this);
    lv_obj_set_user_data(exit,reinterpret_cast<void *>(uintptr_t(2)));
}
void Dino::onCreate(lv_obj_t *parent){
    screen=c::panel(parent,240,240);lv_obj_set_style_bg_color(screen,lv_color_white(),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(0x535353),0);
    lv_obj_add_event_cb(screen,[](lv_event_t *e){static_cast<Dino *>(lv_event_get_user_data(e))->tapped=true;},LV_EVENT_PRESSED,this);
    createScenery();
    auto ground=c::panel(screen,240,1);lv_obj_set_style_bg_color(ground,lv_color_hex(0x535353),0);lv_obj_set_pos(ground,0,dino::Ground);noTouch(ground);
    rex=lv_image_create(screen);noTouch(rex);
    for(auto &o:obstacles){o=lv_image_create(screen);noTouch(o);lv_obj_set_hidden(o,true);}
    for(auto &bonus:bonuses){
        bonus=c::panel(screen,12,12);lv_obj_set_style_bg_opa(bonus,LV_OPA_TRANSP,0);noTouch(bonus);
        pixel(bonus,4,0,4,12,0xBD7800);pixel(bonus,0,4,12,4,0xBD7800);
        lv_obj_set_hidden(bonus,true);
    }
    for(auto &s:shields){
        s=c::panel(screen,16,18);lv_obj_set_style_bg_opa(s,LV_OPA_TRANSP,0);noTouch(s);
        pixel(s,1,0,14,3,0x31627C);pixel(s,1,3,3,8,0x31627C);pixel(s,12,3,3,8,0x31627C);
        pixel(s,3,11,10,3,0x31627C);pixel(s,6,14,4,3,0x31627C);
        pixel(s,4,3,8,8,0x89D1D3);pixel(s,6,4,2,6,0xE0F0DD);lv_obj_set_hidden(s,true);
    }
    shieldGlow=c::panel(screen,47,39);lv_obj_set_style_bg_opa(shieldGlow,LV_OPA_TRANSP,0);noTouch(shieldGlow);
    for(int side=0;side<2;++side)for(int ring=0;ring<2;++ring){
        const int x=side?44-ring*4:ring*4;
        pixel(shieldGlow,x,11,2,17,0x52A7AC);
        pixel(shieldGlow,x+(side?-2:2),6,2,5,0x52A7AC);
        pixel(shieldGlow,x+(side?-2:2),28,2,5,0x52A7AC);
    }
    lv_obj_set_hidden(shieldGlow,true);
    score=c::label(screen,"HI 00000   00000",190);lv_obj_set_pos(score,44,12);lv_obj_set_style_text_font(score,&lv_font_montserrat_14,0);
    auto pauseButton=game_button(screen,"",0,0,36,36,lv_color_white(),lv_color_hex(0x535353),lv_color_hex(0xBACABE),0,[](lv_event_t *e){static_cast<Dino *>(lv_event_get_user_data(e))->pause();},this);
    pixel(pauseButton,8,8,4,16,0x535353);pixel(pauseButton,17,8,4,16,0x535353);
    hint=c::label(screen,"点击跳跃 · 拾取道具加分",232);lv_obj_set_pos(hint,4,208);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
    createMenu();
    screenSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &){pause();});
    timer=lv_timer_create([](lv_timer_t *t){static_cast<Dino *>(lv_timer_get_user_data(t))->tick();},80,this);
    paint();pause();
}
void Dino::saveScore(){sys::Settings::recordDinoScore(game.score);}
void Dino::sound(bool music,unsigned effect){
    if(audio && audio->failed.load())audio.reset();
    if(!audio && music && visible)audio=core::game_audio::start();
    if(audio){audio->setMusic(music);if(effect)audio->trigger(effect);}
}
void Dino::start(){sound(true,core::game_audio::Jump);saveScore();game.reset();cloudOffset=mountainOffset=ridgeOffset=stoneOffset=0;paused=false;tapped=false;lastTick=lv_tick_get();accumulator=0;checkpoint=lastTick;lv_obj_set_hidden(menu,true);lv_obj_set_hidden(hint,false);c::setText(hint,"");game.jump();if(timer)lv_timer_set_period(timer,16);}
void Dino::pause(){sound(false);paused=true;tapped=false;saveScore();accumulator=0;
    if(timer)lv_timer_set_period(timer,80);
    if(menu){
        c::setText(menuText,!game.frames?"小恐龙":game.alive?"已暂停":"游戏结束");
        char text[80];std::snprintf(text,sizeof(text),"得分 %lu\n最高 %lu",static_cast<unsigned long>(game.score),static_cast<unsigned long>(sys::Settings::dinoHighScore()));
        c::setText(menuDetail,game.frames?text:"触屏跳跃\n拾取道具加分");
        c::setText(lv_obj_get_child(menuResume,0),game.alive?"继续":game.frames?"再来一次":"开始");
        lv_obj_set_hidden(hint,true);lv_obj_set_hidden(menu,false);
    }}
bool Dino::onBack(){if(!paused&&game.alive){pause();return true;}return false;}
void Dino::paint(){
    moveScenery();
    const unsigned frame=!game.alive&&game.frames?3:game.jumping?0:(game.frames/6)%2+1;
    if(frame!=spriteFrame){spriteFrame=frame;lv_image_set_src(rex,&dino::sprites::Rex[frame]);}
    lv_obj_set_pos(rex,dino::X,int(game.y));
    lv_obj_set_hidden(rex,game.invulnerable && (game.frames/5)%2);
    lv_obj_set_pos(shieldGlow,dino::X-8,int(game.y)-3);lv_obj_set_hidden(shieldGlow,!game.shield);
    for(unsigned i=0;i<3;++i){const auto &o=game.obstacles[i];
        lv_obj_set_hidden(shields[i],!game.frames || !o.shieldItem || o.x<-o.width);
        const auto box=game.shieldBox(o);lv_obj_set_pos(shields[i],int(box.x),int(box.y));
        if(!game.frames||o.x<-o.width){lv_obj_set_hidden(obstacles[i],true);continue;}
        lv_obj_set_hidden(obstacles[i],false);
        if(obstacleKinds[i]!=o.kind){obstacleKinds[i]=o.kind;lv_image_set_src(obstacles[i],&dino::sprites::Cactus[o.kind]);}
        lv_obj_set_pos(obstacles[i],int(o.x),dino::Ground-o.height);
    }
    for(unsigned i=0;i<3;++i){const auto &b=game.bonuses[i];
        if(!b.active){lv_obj_set_hidden(bonuses[i],true);continue;}
        if(bonusKinds[i]!=b.points){
            bonusKinds[i]=b.points;const auto color=lv_color_hex(b.points==50?0x217EA3:0xBD7800);
            for(unsigned j=0;j<2;++j)lv_obj_set_style_bg_color(lv_obj_get_child(bonuses[i],j),color,0);
        }
        lv_obj_set_pos(bonuses[i],int(b.x),int(b.y));lv_obj_set_hidden(bonuses[i],false);
    }
    if(game.alive){
        char bonusText[32]="";
        if(game.lastBonus&&game.frames-game.bonusFrame<50)std::snprintf(bonusText,sizeof(bonusText),"+%u",game.lastBonus);
        c::setText(hint,bonusText);
    }
    char text[64];const auto best=std::max(game.score,sys::Settings::dinoHighScore());
    std::snprintf(text,sizeof(text),"HI %05lu   %05lu",static_cast<unsigned long>(best),static_cast<unsigned long>(game.score));c::setText(score,text);
}
void Dino::tick(){
    if(!visible)return;
    if(exitPending){saveScore();ui::WindowManager::instance().goHome();return;}
    if(!sys::Power::isScreenOn()||sys::Power::sleepPending()){if(!paused)pause();tapped=resumePending=restartPending=false;return;}
    if(restartPending){restartPending=false;start();}
    if(resumePending){resumePending=false;if(!game.alive)start();else{sound(true);paused=false;lastTick=lv_tick_get();accumulator=0;lv_obj_set_hidden(menu,true);lv_obj_set_hidden(hint,false);if(timer)lv_timer_set_period(timer,16);}}
    if(tapped){tapped=false;if(!game.alive){if(!game.frames||uint32_t(lv_tick_get()-endedAt)>600)start();}else if(!paused&&!game.jumping){game.jump();sound(true,core::game_audio::Jump);}}
    const uint32_t now=lv_tick_get();const auto delta=now-lastTick;lastTick=now;
    if(paused||!game.alive)return;
    lv_display_trigger_activity(nullptr);accumulator+=std::min<uint32_t>(delta,48);
    while(accumulator>=dino::StepMs){
        accumulator-=dino::StepMs;
        const unsigned oldBonus=game.bonusFrame;
        const unsigned oldShield=game.shieldFrame,oldHit=game.shieldHitFrame;
        if(game.step(esp_random())){endedAt=now;pause();sound(false,core::game_audio::Lost);break;}
        if(game.bonusFrame!=oldBonus)sound(true,core::game_audio::Bonus);
        if(game.shieldFrame!=oldShield)sound(true,core::game_audio::Bonus);
        if(game.shieldHitFrame!=oldHit)sound(true,core::game_audio::Impact);
        cloudOffset+=game.speed*.06f;if(cloudOffset>=480)cloudOffset-=480;
        ridgeOffset+=game.speed*.12f;if(ridgeOffset>=440)ridgeOffset-=440;
        mountainOffset+=game.speed*.22f;if(mountainOffset>=450)mountainOffset-=450;
        stoneOffset+=game.speed;if(stoneOffset>=351)stoneOffset-=351;
    }
    if(now-checkpoint>=5000){checkpoint=now;saveScore();}
    paint();
}
void Dino::onShow(){visible=true;audio=core::game_audio::start();keySession=sys::Buttons::claimPageKeys();lastTick=lv_tick_get();if(timer)lv_timer_resume(timer);}
void Dino::onHide(){visible=false;if(audio){audio->requestStop();audio.reset();}saveScore();tapped=false;sys::Buttons::releasePageKeys(keySession);keySession=0;if(timer)lv_timer_pause(timer);}
void Dino::onDestroy(){onHide();sys::EventBus::unsubscribe(screenSub);screenSub=0;if(timer)lv_timer_delete(timer);timer=nullptr;}
} // namespace apps
