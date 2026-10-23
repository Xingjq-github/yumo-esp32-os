#pragma once
#include "lvgl.h"
#include "text_input_layout.hpp"
namespace ui::compact {
constexpr int kWidth=240, kHeight=240, kHeader=36, kRow=40, kTouch=36, kGap=1, kInput=TextInputLayout::Height;
lv_obj_t *panel(lv_obj_t *parent,int width,int height,bool scroll=false);
lv_obj_t *label(lv_obj_t *parent,const char *text,int width=LV_SIZE_CONTENT);
lv_obj_t *button(lv_obj_t *parent,const char *text,lv_event_cb_t cb,void *context,int width=LV_PCT(100),int height=kRow);
lv_obj_t *row(lv_obj_t *parent,const char *title,const char *value,lv_event_cb_t cb,void *context,lv_obj_t **valueLabel=nullptr);
lv_obj_t *toggle(lv_obj_t *parent,const char *title,bool checked,lv_event_cb_t cb,void *context);
lv_obj_t *section(lv_obj_t *parent,const char *text);
lv_obj_t *field(lv_obj_t *parent,const char *placeholder,const char *text,uint32_t maxLength);
lv_obj_t *networkField(lv_obj_t *parent,const char *placeholder,const char *text,uint32_t maxLength,const lv_font_t *font=nullptr);
lv_obj_t *dropdown(lv_obj_t *parent,const char *options,int width);
void setText(lv_obj_t *label,const char *text);
void buttonBackground(lv_obj_t *button,lv_color_t color);
void checked(lv_obj_t *object,bool value);
void disabled(lv_obj_t *object,bool value);
} // namespace ui::compact
