#pragma once
#include "ui/app_icons.hpp"
#include "app.hpp"
#include "dino_model.hpp"
#include "core/game_audio.hpp"
namespace apps {
class Dino : public ui::App {
public:
    const char *name() const override{return "小恐龙";}
    const char *icon() const override{return ui::icons::Dino;}
    bool handlesHorizontalGesture() const override{return true;}
    bool allowsQuickPanel() const override {return paused;}
    uint32_t powerHints() const override {return game.alive?sys::power::KeepSession|(!paused?uint32_t(sys::power::StayLit):0U):0;}
    void onCreate(lv_obj_t *) override;void onShow() override;void onHide() override;void onDestroy() override;bool onBack() override;
private:
    void sound(bool music,unsigned effect=0);
    std::shared_ptr<core::game_audio::Session> audio;
    void tick();void start();void pause();void paint();void saveScore();void createScenery();void moveScenery();void createMenu();
    static void menuAction(lv_event_t *);
    lv_obj_t *screen=nullptr,*rex=nullptr,*obstacles[3]{},*score=nullptr,*hint=nullptr,*menu=nullptr,*menuText=nullptr;
    lv_obj_t *menuDetail=nullptr,*menuResume=nullptr;
    lv_obj_t *clouds[3]{},*mountains[3]{},*ridges[2]{},*stones[9]{},*bonuses[3]{};
    lv_obj_t *shields[3]{},*shieldGlow=nullptr;
    float cloudOffset=0,mountainOffset=0,ridgeOffset=0,stoneOffset=0;
    unsigned bonusKinds[3]{};
    lv_timer_t *timer=nullptr;
    dino::Game game{};
    uint32_t keySession=0,screenSub=0,lastTick=0,accumulator=0,checkpoint=0,endedAt=0;
    bool visible=false,paused=true,tapped=false,resumePending=false,restartPending=false,exitPending=false;
    unsigned spriteFrame=99;
    int obstacleKinds[3]{-1,-1,-1};
};
}
