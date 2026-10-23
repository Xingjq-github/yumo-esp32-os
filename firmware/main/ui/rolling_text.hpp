#pragma once
#include "rolling_text.h"
namespace ui {
class RollingText {
public:
    void create(lv_obj_t *parent,int x,int y,int width,int height,const lv_font_t *font,lv_color_t color){ui_rolling_create(&state,parent,x,y,width,height,font,color);}
    void set(const char *text,bool animated){ui_rolling_set(&state,text,animated);}
    void stop(){ui_rolling_stop(&state);}
private:
    ui_rolling_text_t state{};
};
}
