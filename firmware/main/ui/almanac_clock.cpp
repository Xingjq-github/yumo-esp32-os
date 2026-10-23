#include "almanac_clock.hpp"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "core/almanac_model.hpp"
#include <cstdio>
LV_FONT_DECLARE(lv_font_almanac_24);

namespace ui {
namespace cal=calendar;
void AlmanacClock::create(lv_obj_t *parent) {
    area=compact::panel(parent,240,240);lv_obj_set_pos(area,0,0);
    // All five themes share this exact geometry; only palette colors change.
    auto middle=compact::panel(area,232,83);lv_obj_set_pos(middle,4,84);
    lv_obj_set_style_radius(middle,6,0);lv_obj_set_style_bg_color(middle,theme::surface(),0);
    rows[0].create(area,4,2,232,24,theme::fontSmall(),theme::muted());
    rows[1].create(area,4,28,232,32,&lv_font_almanac_24,theme::fg());
    rows[2].create(area,4,61,232,22,theme::fontSmall(),theme::muted());
    rows[3].create(area,4,87,232,38,&lv_font_almanac_24,theme::accent());
    rows[4].create(area,4,130,232,32,&lv_font_almanac_24,theme::fg());
    rows[5].create(area,4,169,232,22,theme::fontSmall(),theme::accent());
    rows[6].create(area,4,192,232,22,theme::fontMenu(),theme::muted());
    ui_rolling_clock_create(&clock,area,4,216,232,24,theme::fontSmall(),theme::fg());
}
void AlmanacClock::show(lv_obj_t *parent){if(!area)create(parent);lv_obj_set_hidden(area,false);}
void AlmanacClock::stop(){for(auto &row:rows)row.stop();ui_rolling_clock_stop(&clock);}
void AlmanacClock::hide(){stop();if(area)lv_obj_set_hidden(area,true);}
void AlmanacClock::refresh(const tm &t,bool animated) {
    if(!area)return;
    ui_rolling_clock_set(&clock,t.tm_hour,t.tm_min,t.tm_sec,animated);
    // Astronomical/calendar fields change on minute boundaries at most; the
    // seconds clock remains independent and keeps per-character transitions.
    const int64_t minute=((((int64_t(t.tm_year)*367+t.tm_yday)*24)+t.tm_hour)*60)+t.tm_min;
    if(calendarMinute==minute)return;
    calendarMinute=minute;
    const cal::Date date{t.tm_year+1900,t.tm_mon+1,t.tm_mday};
    const auto a=cal::almanac(date,t.tm_hour,t.tm_min,t.tm_sec);
    if(!a){stop();for(auto &row:rows)row.set("",false);ui_rolling_clock_clear(&clock);rows[0].set("请先设置设备日期",false);rows[3].set("黄历",false);return;}
    const auto l=a.lunarDate;const auto year=cal::cyclePillar(l.year-4);
    char text[160];
    std::snprintf(text,sizeof(text),"农历 %s%s年  生肖%s",cal::Stems[year.stem],cal::Branches[year.branch],cal::Animals[year.branch]);rows[0].set(text,animated);
    std::snprintf(text,sizeof(text),"%s%s%s",l.leap?"闰":"",cal::Months[l.month-1],cal::Days[l.day-1]);rows[1].set(text,animated);
    std::snprintf(text,sizeof(text),"节气 %s%s%s",cal::Terms[a.solarTerm],*cal::festival(date)?"  ":"",cal::festival(date));rows[2].set(text,animated);
    std::snprintf(text,sizeof(text),"%s时  %s",cal::Branches[a.time.branch],cal::HourNames[a.time.branch]);rows[3].set(text,animated);
    std::snprintf(text,sizeof(text),"%s%s %s%s %s%s %s%s",cal::Stems[a.year.stem],cal::Branches[a.year.branch],cal::Stems[a.month.stem],cal::Branches[a.month.branch],
        cal::Stems[a.day.stem],cal::Branches[a.day.branch],cal::Stems[a.time.stem],cal::Branches[a.time.branch]);rows[4].set(text,animated);
    std::snprintf(text,sizeof(text),"时星神  %s",cal::StarGods[a.timeGod]);rows[5].set(text,animated);
    std::snprintf(text,sizeof(text),"天干:%s 地支:%s 纳音:%s",cal::StemElements[a.time.stem],cal::BranchElements[a.time.branch],cal::NaYin[a.time.cycle()/2]);rows[6].set(text,animated);
}
}
