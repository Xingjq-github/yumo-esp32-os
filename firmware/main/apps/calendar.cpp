#include "calendar.hpp"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "core/almanac_model.hpp"
#include "ui/control_states.h"
#include <cstdio>
#include <string>
LV_FONT_DECLARE(lv_font_clock_84);
LV_FONT_DECLARE(lv_font_almanac_24);
namespace apps {
namespace c=ui::compact;
namespace cal=calendar;
namespace {
cal::Date deviceDate() {const auto n=time(nullptr);tm t{};localtime_r(&n,&t);return {t.tm_year+1900,t.tm_mon+1,t.tm_mday};}
void center(lv_obj_t *o) {lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);}
void bubbleTouches(lv_obj_t *obj) {
    lv_obj_set_event_bubble(obj,true);lv_obj_set_press_lock(obj,true);
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)bubbleTouches(lv_obj_get_child(obj,i));
}
void decoration(lv_obj_t *obj) {lv_obj_set_clickable(obj,false);lv_obj_set_scrollable(obj,false);}
}
void Calendar::onCreate(lv_obj_t *parent) {
    lv_obj_set_scrollable(parent,false);
    // Stop LVGL gesture-target lookup at this window.
    lv_obj_set_gesture_bubble(parent,false);
    auto back=c::button(parent,LV_SYMBOL_HOME,[](lv_event_t *e){if(!static_cast<Calendar *>(lv_event_get_user_data(e))->suppressClick)sys::EventBus::publish({sys::EventType::BackRequested});},this,34,36);
    lv_obj_set_pos(back,0,0);
    auto prev=c::button(parent,LV_SYMBOL_LEFT,[](lv_event_t *e){auto s=static_cast<Calendar *>(lv_event_get_user_data(e));if(!s->suppressClick)--s->movePending;},this,34,36);
    lv_obj_set_pos(prev,35,0);
    title=c::button(parent,"0000.00",[](lv_event_t *e){auto s=static_cast<Calendar *>(lv_event_get_user_data(e));if(!s->suppressClick)s->pickerPending=true;},this,94,36);
    lv_obj_set_pos(title,70,0);
    auto next=c::button(parent,LV_SYMBOL_RIGHT,[](lv_event_t *e){auto s=static_cast<Calendar *>(lv_event_get_user_data(e));if(!s->suppressClick)++s->movePending;},this,34,36);
    lv_obj_set_pos(next,165,0);
    auto now=c::button(parent,"今",[](lv_event_t *e){auto s=static_cast<Calendar *>(lv_event_get_user_data(e));if(!s->suppressClick)s->today();},this,40,36);
    lv_obj_set_pos(now,200,0);
    const char *week[]={"一","二","三","四","五","六","日"};
    for(int i=0;i<7;++i) {auto label=c::label(parent,week[i],34);center(label);lv_obj_set_pos(label,i*34+1,36);}
    for(int i=0;i<42;++i) {
        cells[i]=c::button(parent,"",dayCb,this,33,30);lv_obj_set_pos(cells[i],i%7*34+1,54+i/7*31);
        lv_obj_set_style_border_width(cells[i],0,0);lv_obj_set_style_pad_all(cells[i],0,0);
        lv_obj_set_user_data(cells[i],reinterpret_cast<void *>(uintptr_t(i+1)));
        numbers[i]=c::label(cells[i],"",33);center(numbers[i]);lv_obj_set_pos(numbers[i],0,0);
        lv_obj_set_style_text_font(numbers[i],&lv_font_montserrat_12,0);
        lunarText[i]=c::label(cells[i],"",33);center(lunarText[i]);lv_obj_set_pos(lunarText[i],0,13);
        lv_obj_set_style_text_font(lunarText[i],ui::theme::fontMenu(),0);lv_obj_set_y(lunarText[i],12);
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);++i)bubbleTouches(lv_obj_get_child(parent,i));
    lv_obj_set_press_lock(parent,true);
    lv_obj_add_event_cb(parent,touchCb,LV_EVENT_ALL,this);
    lv_obj_add_event_cb(parent,gestureCb,LV_EVENT_GESTURE,this);
    timeSub=sys::EventBus::subscribe(sys::EventType::TimeSynced,[this](const sys::Event &){lastSecond=-1;redraw=true;});
    timer=lv_timer_create([](lv_timer_t *t){static_cast<Calendar *>(lv_timer_get_user_data(t))->tick();},100,this);
    today();updateMonth();
}
void Calendar::today() {current=deviceDate();if(cal::valid(current))selected=current;redraw=true;}
void Calendar::updateMonth() {
    char text[32];std::snprintf(text,sizeof(text),"%04d.%02d",selected.year,selected.month);
    c::setText(lv_obj_get_child(title,0),text);
    const int first=(cal::weekday({selected.year,selected.month,1})+6)%7;
    for(int i=0;i<42;++i) {
        const int day=i-first+1;const bool enabled=day>=1&&day<=cal::monthDays(selected.year,selected.month);
        if(!enabled) {lv_obj_set_hidden(cells[i],true);continue;}
        lv_obj_set_hidden(cells[i],false);
        const cal::Date d{selected.year,selected.month,day};const auto l=cal::lunar(d);
        std::snprintf(text,sizeof(text),"%d",day);c::setText(numbers[i],text);
        const char *subtitle=cal::term(d);if(!*subtitle)subtitle=cal::festival(d);
        const bool special=*subtitle;
        if(!special)subtitle=l.day==1?cal::Months[l.month-1]:cal::Days[l.day-1];
        c::setText(lunarText[i],subtitle);
        const bool isToday=cal::equal(d,current);c::checked(cells[i],isToday);
        lv_obj_set_style_text_color(lunarText[i],isToday?ui::theme::onAccent():special?ui::theme::accent():ui::theme::muted(),0);
        lv_obj_set_style_text_color(numbers[i],isToday?ui::theme::onAccent():i%7>=5?ui::theme::accent():ui::theme::fg(),0);
    }
    redraw=false;
}
void Calendar::dayCb(lv_event_t *e) {
    auto self=static_cast<Calendar *>(lv_event_get_user_data(e));
    if(self->suppressClick)return;
    const int cell=int(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))))-1;
    const int first=(cal::weekday({self->selected.year,self->selected.month,1})+6)%7;
    self->detailPending=cell-first+1;
}
void Calendar::gestureCb(lv_event_t *event) {
    // Horizontal navigation belongs to this App. Use press displacement below,
    // not LVGL's velocity-sensitive gesture sum (which resets on stationary samples).
    lv_event_stop_bubbling(event);
}
void Calendar::touchCb(lv_event_t *event) {
    const auto code=lv_event_get_code(event);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&
       code!=LV_EVENT_PRESS_LOST&&code!=LV_EVENT_INDEV_RESET)return;
    auto self=static_cast<Calendar *>(lv_event_get_user_data(event));
    // A modal handles its own contact exactly once, never again at the App root.
    lv_event_stop_bubbling(event);
    if(code==LV_EVENT_PRESS_LOST||code==LV_EVENT_INDEV_RESET) {
        self->tracking=false;self->suppressClick=true;return;
    }
    auto indev=lv_indev_active();
    if(!indev||!self->visible||!sys::Power::isScreenOn()||self->yearPicker||self->closePending) {
        self->tracking=false;return;
    }
    lv_point_t point;lv_indev_get_point(indev,&point);
    if(code==LV_EVENT_PRESSED) {
        self->pressX=point.x;self->pressY=point.y;
        self->tracking=true;self->suppressClick=false;return;
    }
    if(!self->tracking)return;
    const int dx=point.x-self->pressX,dy=point.y-self->pressY;
    const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    if(ax>=12||ay>=12)self->suppressClick=true;
    if(code!=LV_EVENT_RELEASED)return;
    self->tracking=false;
    // Decide once on release. Keep the stable touch target and never latch the
    // whole input device in wait-release while a page is being replaced.
    if(ax>=30&&ax>ay*3/2)self->movePending=dx<0?1:-1;
}
void Calendar::stopFlip() {
    lv_anim_delete(this,flipFrame);
    if(flipping&&detailPage){updateDetail();lv_obj_set_x(detailPage,0);}
    flipping=false;flipUpdated=false;
}
void Calendar::closeDialog() {
    stopFlip();movePending=0;tracking=false;
    if(dialog)lv_obj_delete(dialog);
    dialog=yearPicker=monthPicker=detailPage=detailHeading=nullptr;
    for(auto &label:detailText)label=nullptr;
}
bool Calendar::onBack() {if(dialog){closePending=true;return true;}return false;}
void Calendar::showDetail(int day) {
    closeDialog();cal::Date d{selected.year,selected.month,day};if(!cal::valid(d))return;
    selected.day=day;redraw=true;dialog=c::panel(root(),240,240);
    lv_obj_set_gesture_bubble(dialog,false);lv_obj_set_press_lock(dialog,true);
    lv_obj_add_event_cb(dialog,touchCb,LV_EVENT_ALL,this);
    lv_obj_add_event_cb(dialog,gestureCb,LV_EVENT_GESTURE,this);
    const auto ink=ui::theme::fg(),highlight=ui::theme::accent();
    const auto headingInk=ui::theme::onAccent(),rule=ui::theme::edge();
    auto strip=c::panel(dialog,240,36);lv_obj_set_style_bg_color(strip,highlight,0);decoration(strip);
    auto back=c::button(dialog,LV_SYMBOL_LEFT,[](lv_event_t *e){
        auto self=static_cast<Calendar *>(lv_event_get_user_data(e));
        if(!self->suppressClick)self->closePending=true;
    },this,40,36);
    lv_obj_set_pos(back,0,0);lv_obj_set_style_bg_color(back,highlight,0);lv_obj_set_style_border_width(back,0,0);
    ui_control_ink(back,headingInk,headingInk);lv_obj_set_style_bg_color(back,lv_color_mix(highlight,ink,220),LV_STATE_PRESSED);
    bubbleTouches(back);
    detailHeading=c::label(dialog,"",156);center(detailHeading);lv_obj_set_pos(detailHeading,44,7);
    lv_obj_set_style_text_color(detailHeading,headingInk,0);
    // A folded paper corner, drawn without a button shape, hit target or image buffer.
    auto fold=c::panel(dialog,25,25);lv_obj_set_pos(fold,215,0);decoration(fold);
    lv_obj_set_style_bg_opa(fold,LV_OPA_TRANSP,0);
    lv_obj_add_event_cb(fold,[](lv_event_t *e){
        lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
        lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.width=1;
        line.color=lv_color_mix(ui::theme::onAccent(),ui::theme::accent(),80);
        for(int y=0;y<24;++y){line.p1.x=a.x1+y;line.p1.y=a.y1+y;line.p2.x=a.x2;line.p2.y=a.y1+y;lv_draw_line(lv_event_get_layer(e),&line);}
        line.color=lv_color_mix(ui::theme::onAccent(),ui::theme::accent(),150);
        line.p1.x=a.x1;line.p1.y=a.y1;line.p2.x=a.x2;line.p2.y=a.y2;lv_draw_line(lv_event_get_layer(e),&line);
    },LV_EVENT_DRAW_MAIN,nullptr);
    detailPage=c::panel(dialog,240,204);lv_obj_set_pos(detailPage,0,36);decoration(detailPage);
    auto textAt=[&](int index,int x,int y,int width,const lv_font_t *font,lv_color_t color) {
        auto label=c::label(detailPage,"",width);center(label);lv_obj_set_pos(label,x,y-36);
        lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,color,0);detailText[index]=label;
    };
    const int dayY=36+(100-lv_font_clock_84.line_height)/2;
    textAt(0,5,dayY,124,&lv_font_clock_84,highlight);
    textAt(1,137,46,98,ui::theme::fontSmall(),ink);
    textAt(2,137,74,98,ui::theme::fontSmall(),highlight);
    textAt(3,137,102,98,ui::theme::fontSmall(),ink);
    for(int x=6;x<236;x+=10){auto dash=c::panel(detailPage,5,1);lv_obj_set_pos(dash,x,100);lv_obj_set_style_bg_color(dash,rule,0);decoration(dash);}
    textAt(4,4,145,232,ui::theme::fontSmall(),ink);
    textAt(5,4,170,232,&lv_font_almanac_24,highlight);
    textAt(6,4,207,232,ui::theme::fontSmall(),ink);
    for(int y:{197,201}){auto edge=c::panel(detailPage,232,1);lv_obj_set_pos(edge,4,y);lv_obj_set_style_bg_color(edge,rule,0);decoration(edge);}
    updateDetail();
}
void Calendar::updateDetail() {
    if(!detailPage)return;
    const auto d=selected;const auto l=cal::lunar(d);const auto a=cal::dateAlmanac(d);
    char text[256];const char *week[]={"日","一","二","三","四","五","六"};
    std::snprintf(text,sizeof(text),"%04d年%02d月",d.year,d.month);c::setText(detailHeading,text);
    std::snprintf(text,sizeof(text),"%02d",d.day);c::setText(detailText[0],text);
    std::snprintf(text,sizeof(text),"星期%s",week[cal::weekday(d)]);c::setText(detailText[1],text);
    c::setText(detailText[2],cal::constellation(d));
    c::setText(detailText[3],*cal::festival(d)?cal::festival(d):cal::Terms[a.solarTerm]);
    std::snprintf(text,sizeof(text),"农历 %s%s%s年  生肖%s",cal::Stems[(l.year-4)%10],cal::Branches[(l.year-4)%12],cal::Animals[(l.year-4)%12],cal::Animals[(l.year-4)%12]);
    c::setText(detailText[4],text);
    std::snprintf(text,sizeof(text),"%s%s%s",l.leap?"闰":"",cal::Months[l.month-1],cal::Days[l.day-1]);c::setText(detailText[5],text);
    std::snprintf(text,sizeof(text),"%s%s年 %s%s月 %s%s日",cal::Stems[a.year.stem],cal::Branches[a.year.branch],cal::Stems[a.month.stem],cal::Branches[a.month.branch],cal::Stems[a.day.stem],cal::Branches[a.day.branch]);
    c::setText(detailText[6],text);
}
void Calendar::flipFrame(void *context,int32_t value) {
    auto self=static_cast<Calendar *>(context);
    if(!self->detailPage)return;
    if(value>=240&&!self->flipUpdated){self->updateDetail();self->flipUpdated=true;}
    lv_obj_set_x(self->detailPage,self->flipDirection*(value<240?-value:480-value));
}
void Calendar::flipDone(lv_anim_t *anim) {
    auto self=static_cast<Calendar *>(anim->var);
    if(self->detailPage){self->updateDetail();lv_obj_set_x(self->detailPage,0);}
    self->flipping=false;
}
void Calendar::flipDay(int direction) {
    const auto date=cal::moveDay(selected,direction);
    if(cal::equal(date,selected))return;
    selected=date;redraw=true;flipDirection=direction;flipping=true;flipUpdated=false;
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,this);
    lv_anim_set_values(&animation,0,480);lv_anim_set_duration(&animation,220);
    lv_anim_set_exec_cb(&animation,flipFrame);lv_anim_set_completed_cb(&animation,flipDone);
    if(!lv_anim_start(&animation)){updateDetail();lv_obj_set_x(detailPage,0);flipping=false;}
}
void Calendar::showPicker() {
    closeDialog();dialog=c::panel(root(),240,240);auto text=c::label(dialog,"选择年月",230);lv_obj_set_pos(text,5,16);
    std::string years;for(int y=cal::FirstYear;y<=cal::LastYear;++y){if(!years.empty())years+='\n';years+=std::to_string(y);}
    yearPicker=c::dropdown(dialog,years.c_str(),119);lv_obj_set_pos(yearPicker,0,62);lv_dropdown_set_selected(yearPicker,selected.year-cal::FirstYear);
    monthPicker=c::dropdown(dialog,"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12",120);lv_obj_set_pos(monthPicker,120,62);lv_dropdown_set_selected(monthPicker,selected.month-1);
    auto hint=c::label(dialog,"左右滑动翻月，点击日期查看详情。",236);lv_obj_set_pos(hint,2,116);lv_label_set_long_mode(hint,LV_LABEL_LONG_MODE_WRAP);
    auto cancel=c::button(dialog,"取消",[](lv_event_t *e){static_cast<Calendar *>(lv_event_get_user_data(e))->closePending=true;},this,119,40);lv_obj_set_pos(cancel,0,200);
    auto ok=c::button(dialog,"确定",[](lv_event_t *e){auto s=static_cast<Calendar *>(lv_event_get_user_data(e));s->selected={cal::FirstYear+int(lv_dropdown_get_selected(s->yearPicker)),1+int(lv_dropdown_get_selected(s->monthPicker)),1};s->closePending=s->redraw=true;},this,120,40);lv_obj_set_pos(ok,120,200);
}
void Calendar::tick() {
    if(!visible||!sys::Power::isScreenOn()) {
        tracking=false;movePending=0;stopFlip();return;
    }
    if(closePending){closeDialog();closePending=false;}
    if(pickerPending){showPicker();pickerPending=false;}
    if(detailPending){showDetail(detailPending);detailPending=0;}
    if(movePending&&!flipping) {
        const int delta=movePending;movePending=0;
        if(!dialog){selected=cal::moveMonth(selected,delta);redraw=true;}
        else if(!yearPicker) {
            flipDay(delta);
        }
    }
    const auto now=time(nullptr);if(now!=lastSecond){lastSecond=now;auto date=deviceDate();if(!cal::equal(date,current)){current=date;redraw=true;}}
    if(redraw)updateMonth();
}
void Calendar::onShow(){visible=true;if(timer)lv_timer_resume(timer);}
void Calendar::onHide(){visible=false;tracking=false;movePending=0;stopFlip();if(timer)lv_timer_pause(timer);}
void Calendar::onDestroy(){onHide();if(timer)lv_timer_delete(timer);timer=nullptr;sys::EventBus::unsubscribe(timeSub);timeSub=0;}
} // namespace apps
