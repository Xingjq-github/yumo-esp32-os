#pragma once
#include "core/xiaozhi_service.hpp"
#include "ui/app.hpp"
#include "ui/app_icons.hpp"

namespace apps {
class Xiaozhi final : public ui::App {
public:
    const char *name() const override { return "小智AI"; }
    const char *icon() const override { return ui::icons::Xiaozhi; }
    void onCreate(lv_obj_t *) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    uint32_t powerHints() const override;
private:
    void connect();
    void action();
    void suspend();
    void poll();
    void render(const core::xiaozhi::Snapshot &,bool finished);
    static void tick(lv_timer_t *);
    static void click(lv_event_t *);
    static void bodyEvent(lv_event_t *);
    std::shared_ptr<core::xiaozhi::Job> job;
    core::xiaozhi::Snapshot last;
    lv_obj_t *status=nullptr,*statusDot=nullptr,*meter=nullptr,*body=nullptr;
    lv_obj_t *welcome=nullptr,*welcomeText=nullptr;
    lv_obj_t *user=nullptr,*answer=nullptr,*binding=nullptr,*bindingCode=nullptr,*bindingHint=nullptr;
    lv_obj_t *talk=nullptr,*talkText=nullptr;
    lv_timer_t *timer=nullptr;
    uint32_t pageSession=0,backSession=0,nextSub=0,offSub=0;
    bool visible=false,stopped=false,haveSnapshot=false,finished=false,touchingBody=false;
};
}
