#pragma once
#include "rolling_text.h"
#include <stdint.h>

// Two Chinese digits per hour/minute/second field (零九 -> 一零 -> 一一).
// Unit labels and unchanged digits stay fixed while changed digits roll.
typedef struct { ui_rolling_text_t cells[9]; } ui_rolling_clock_t;
static inline void ui_rolling_clock_create(ui_rolling_clock_t *r,lv_obj_t *parent,int x,int y,int width,int height,const lv_font_t *font,lv_color_t color) {
    const int cell=16,start=x+(width-9*cell)/2;
    for(int i=0;i<9;++i)ui_rolling_create(&r->cells[i],parent,start+i*cell,y,cell,height,font,color);
}
static inline void ui_rolling_clock_number(ui_rolling_clock_t *r,int group,unsigned value,bool animated) {
    static const char *const digits[]={"零","一","二","三","四","五","六","七","八","九"};
    ui_rolling_set(&r->cells[group*3],digits[value/10],animated);
    ui_rolling_set(&r->cells[group*3+1],digits[value%10],animated);
}
static inline void ui_rolling_clock_set(ui_rolling_clock_t *r,unsigned hour,unsigned minute,unsigned second,bool animated) {
    if(hour>23||minute>59||second>59)return;
    ui_rolling_clock_number(r,0,hour,animated);
    ui_rolling_clock_number(r,1,minute,animated);
    ui_rolling_clock_number(r,2,second,animated);
    ui_rolling_set(&r->cells[2],"时",false);ui_rolling_set(&r->cells[5],"分",false);ui_rolling_set(&r->cells[8],"秒",false);
}
static inline void ui_rolling_clock_stop(ui_rolling_clock_t *r) {for(int i=0;i<9;++i)ui_rolling_stop(&r->cells[i]);}
static inline void ui_rolling_clock_clear(ui_rolling_clock_t *r) {for(int i=0;i<9;++i)ui_rolling_set(&r->cells[i],"",false);}
