#pragma once
#include "ui/app.hpp"
#include "ui/app_icons.hpp"
#include "core/stopwatch_model.hpp"
#include "core/timer_key_state.hpp"

namespace apps {
class Timer final : public ui::App {
public:
    const char *name() const override {return "计时器";}
    const char *icon() const override {return ui::icons::Timer;}
    bool allowsQuickPanel() const override {return false;}
    uint32_t powerHints() const override {return visible?sys::power::StayLit|sys::power::KeepSession:sys::power::None;}
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
private:
    core::stopwatch::Model model;
    uint32_t keySession=0;
    size_t timeDigits=0;
    bool visible=false,closing=false,inputOverflow=false;
    lv_timer_t *timer=nullptr;
    lv_obj_t *powerButton=nullptr,*keyButton=nullptr,*powerText=nullptr,*keyText=nullptr;
    lv_obj_t *clockGroup=nullptr,*timeText=nullptr,*fractionText=nullptr,*statusText=nullptr;
    lv_obj_t *list=nullptr,*liveRow=nullptr;
    static void tick(lv_timer_t *timer);
    static void touch(lv_event_t *event);
    bool drainKeys();
    void act(sys::TimerKey::Action action,int64_t at);
    void refresh(int64_t now);
    void appendLap();
    void colorLaps();
    void exit();
};
}
