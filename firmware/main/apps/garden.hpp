#pragma once
#include "ui/app.hpp"
#include "ui/app_icons.hpp"
#include "ui/garden_scene.hpp"
#include "core/garden_service.hpp"
#include <array>
#include <string>

namespace apps {
class Garden final : public ui::App {
public:
    const char *name() const override {return "萌芽物语";}
    const char *icon() const override {return ui::icons::Garden;}
    bool allowsQuickPanel() const override {return false;}
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    enum class Page {Loading,Welcome,Home,Care,Train,Explore,Event,Memory,House,Album,Detail,Adult,Rebirth,Error};
    enum Command {Back=1,Adopt,Retry,Temporary,Continue,Confirm,Replay,ToHome,ToCare,ToTrain,ToExplore,ToHouse,ToAlbum,ToRebirth,
                  Feed=30,Rest,Clean,Cuddle,Cook,Play,Plant,Harvest,Training=50,Exploring=60,Choosing=70,Guessing=80,Decorating=90,Preview=100,Interact=120};
    core::garden::State state;
    core::sd::Access access{};
    uint64_t revision=0,generation=0;
    std::shared_ptr<core::garden::Job> job;
    bool visible=false,temporary=false,loaded=false,dirty=false,saveError=false,refreshPending=false;
    int error=0,preview=0;
    Page page=Page::Loading;
    Page drawnPage=Page::Loading;
    // At most one save runs; short touch bursts wait in order on the current page.
    std::array<int,8> pendingCommands{};
    unsigned pendingCount=0;
    std::string notice;
    uint32_t loadSince=0,miniElapsed=0,miniTick=0,backSession=0;
    unsigned miniFrom=0;
    bool miniReset=true;
    uint32_t petStart=0,noticeSince=0,frameSince=0;
    bool petActive=false;
    unsigned petVariant=0;
    unsigned frame=0,buttonCount=0;
    struct Binding {Garden *owner=nullptr;int command=0;lv_obj_t *object=nullptr;};
    std::array<Binding,24> bindings{};
    std::array<lv_obj_t *,4> notes{};
    lv_timer_t *timer=nullptr;
    lv_obj_t *scene=nullptr,*status=nullptr,*memoryStatus=nullptr,*toast=nullptr;
    lv_obj_t *countdown=nullptr,*countdownText=nullptr;
    lv_obj_t *nameplate=nullptr;
    std::array<lv_obj_t *,3> nameSparks{};
    ui::garden_scene::Pixels pixels{};
    ui::garden_scene::View sceneView{};
    lv_image_dsc_t image{};
    static void tick(lv_timer_t *timer);
    static void touch(lv_event_t *event);
    void dispatch(int command);
    void change(core::garden::Action action,unsigned choice=0);
    void beginLoad();
    void persist();
    void poll();
    void show(Page next);
    void draw();
    void landscape();
    void header(const char *title,const char *detail="");
    void homeHeader(const char *name);
    void animateNameplate();
    void resetMemory();
    bool memoryWatching() const;
    void animate(bool renderScene=false);
    void controls();
    void feedback();
    void message(int y=216);
    lv_obj_t *button(lv_obj_t *parent,const char *caption,int command,int x,int y,int w,int h=36,const char *detail=nullptr);
};
}
