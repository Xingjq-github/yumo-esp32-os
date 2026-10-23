#pragma once
#include "rolling_text.hpp"
#include "rolling_clock.h"
#include <time.h>
namespace ui {
class AlmanacClock {
public:
    void show(lv_obj_t *parent);
    void hide();
    void stop();
    void refresh(const tm &time,bool animated);
private:
    void create(lv_obj_t *parent);
    lv_obj_t *area=nullptr;
    int64_t calendarMinute=-1;
    RollingText rows[7];
    ui_rolling_clock_t clock{};
};
}
