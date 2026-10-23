#pragma once
#include "ui/app.hpp"
#include "ui/app_icons.hpp"
#include "core/pattern_store.hpp"
#include "core/pattern_play.hpp"
namespace apps {
class DesignPatterns final:public ui::App {
public:
    const char *name()const override{return "设计模式";}
    const char *icon()const override{return ui::icons::Patterns;}
    void onCreate(lv_obj_t *)override;
    void onShow()override;void onHide()override;void onDestroy()override;
    bool onBack()override;
    bool handlesHorizontalGesture()const override{return true;}
    bool allowsQuickPanel()const override{return page!=Page::Running;}
    uint32_t powerHints()const override;
    void onPowerSaving(bool)override;
private:
    enum class Page {Home,Groups,List,Loading,Brief,Workshop,Running,Result,Info,Code,Help};
    enum class Action:unsigned {Back=1,Continue,Groups,Atlas,Help,Category,Entry,Prev,Next,Start,Node,Run,Hint,Undo,Reset,Info,Code,Retry,Advance,Reload,Resume,Practice};
    static void click(lv_event_t *);static void paint(lv_event_t *);
    void act(Action,unsigned=0);
    lv_obj_t *button(lv_obj_t *,const char *,Action,unsigned,int,int,int,int,bool=false);
    lv_obj_t *label(lv_obj_t *,const std::string &,int,int,int,bool=false);
    lv_obj_t *panel(lv_obj_t *,int,int,int,int);
    void draw();void scene();void poll();void request(unsigned,unsigned,bool=false);void loadProgress();
    void perform(unsigned);void finish();void markDirty();void flush();void stagePage();
    void undo();void primary();void clearContent();void guide();
    lv_obj_t *parent=nullptr,*header=nullptr,*body=nullptr,*footer=nullptr,*board=nullptr,*status=nullptr;
    lv_timer_t *timer=nullptr;
    std::unique_ptr<core::patterns::Lesson> lesson;
    std::shared_ptr<core::patterns::Job> job,saving;
    core::patterns::Progress progress;
    core::sd::Access access;
    core::patterns::play::Game game;
    std::array<core::patterns::play::State,16> history{};
    unsigned historySize=0;
    Page page=Page::Home,infoReturn=Page::Home,codeReturn=Page::Home;
    std::string message;
    uint64_t generation=0;
    uint32_t keys=0,prevSub=0,nextSub=0,screenSub=0,changed=0,stepAt=0,frameAt=0;
    unsigned group=0,listOffset=0,index=0,stage=0,frame=0;
    int highlighted=-1;
    bool hidden=true,dirty=false,ready=false,atlas=false,passed=false,paused=false,keepSession=false,hints=false;
};
}
