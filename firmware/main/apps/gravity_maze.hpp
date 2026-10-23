#pragma once
#include "ui/app_icons.hpp"
#include "app.hpp"
#include "maze_model.hpp"
#include "maze_fog.hpp"
#include "core/game_audio.hpp"
namespace apps {
class GravityMaze : public ui::App {
public:
    const char *name() const override {return "重力迷宫";}
    const char *icon() const override {return ui::icons::Maze;}
    bool handlesHorizontalGesture() const override {return true;}
    bool allowsQuickPanel() const override {return !active;}
    bool locksScreenOrientation() const override {return true;}
    uint32_t powerHints() const override {return hasPlayed && !won?sys::power::KeepSession|(active?uint32_t(sys::power::StayLit):0U):0;}
    void onCreate(lv_obj_t *) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    enum class Action {None,Play,Restart,Select,Next,Menu,Exit,Developer};
    static void actionCb(lv_event_t *);
    void createMenu();void buildBoard();void play();void pause(const char *message=nullptr);void releaseSensor();void tick();
    void paintBall();
    std::shared_ptr<core::game_audio::Session> audio;
    uint16_t *floorPixels=nullptr;
    uint8_t *fogPixels=nullptr;
    uint32_t goalPixels[18*18]{};
    maze::Fog fog;
    bool developer=false;
    lv_image_dsc_t fogImage{},goalImage{};
    lv_obj_t *fogView=nullptr,*badge=nullptr;
    uint32_t ballPixels[144]{};
    lv_image_dsc_t floorImage{},ballImage{};
    lv_obj_t *shadow=nullptr,*badgeBall=nullptr;
    lv_obj_t *screen=nullptr,*boardView=nullptr,*ballView=nullptr,*menu=nullptr,*menuCard=nullptr,*heading=nullptr,*hint=nullptr,*resume=nullptr,*levels=nullptr,*notice=nullptr;
    lv_obj_t *stages[maze::Stages]{},*menuActions[4]{};
    lv_timer_t *timer=nullptr;
    maze::Board board{};maze::Ball ball{};
    unsigned level=0;
    uint32_t lease=0,keySession=0,screenSub=0,started=0,lastSample=0,lastStep=0,lastImpact=0;
    float ax=0,ay=0;
    bool active=false,visible=false,won=false,sampled=false,hasPlayed=false;
    Action pending=Action::None;
};
}
