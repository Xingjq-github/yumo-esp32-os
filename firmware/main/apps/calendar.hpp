#pragma once
#include "app.hpp"
#include "core/calendar_model.hpp"
#include <ctime>

namespace apps {
class Calendar : public ui::App {
public:
    const char *name() const override {return "万年历";}
    const char *icon() const override {return LV_SYMBOL_LIST;}
    bool handlesHorizontalGesture() const override {return true;}
    void onCreate(lv_obj_t *) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    void tick();
    void updateMonth();
    void showDetail(int day);
    void showPicker();
    void closeDialog();
    void today();
    void updateDetail();
    void flipDay(int direction);
    void stopFlip();
    static void dayCb(lv_event_t *);
    static void gestureCb(lv_event_t *);
    static void touchCb(lv_event_t *);
    static void flipFrame(void *,int32_t);
    static void flipDone(lv_anim_t *);
    calendar::Date selected{2026,1,1},current{0,0,0};
    bool visible=false,redraw=false,closePending=false,pickerPending=false;
    int detailPending=0,movePending=0;
    int pressX=0,pressY=0,flipDirection=0;
    bool tracking=false,suppressClick=false,flipping=false,flipUpdated=false;
    lv_obj_t *title=nullptr,*cells[42]{},*numbers[42]{},*lunarText[42]{};
    lv_obj_t *dialog=nullptr,*yearPicker=nullptr,*monthPicker=nullptr;
    lv_obj_t *detailPage=nullptr,*detailHeading=nullptr,*detailText[7]{};
    lv_timer_t *timer=nullptr;
    uint32_t timeSub=0;
    time_t lastSecond=-1;
};
}
