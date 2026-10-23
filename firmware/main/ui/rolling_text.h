#pragma once
#include "lvgl.h"
#include <string.h>

// C-compatible core so the same LVGL animation/lifetime code can be replayed
// on the host. Labels and their viewport live for the owning App's lifetime.
typedef struct {
    lv_obj_t *box,*labels[2];
    int height,offset;
    bool active,initialized;
} ui_rolling_text_t;

static inline void ui_rolling_position(ui_rolling_text_t *r,int value) {
    lv_obj_set_y(r->labels[0],r->offset-value);
    lv_obj_set_y(r->labels[1],r->offset+r->height-value);
}
static inline void ui_rolling_commit(ui_rolling_text_t *r) {
    lv_obj_t *temp=r->labels[0];r->labels[0]=r->labels[1];r->labels[1]=temp;
    r->active=false;ui_rolling_position(r,0);
}
static inline void ui_rolling_animate(void *p,int32_t value){ui_rolling_position((ui_rolling_text_t *)p,value);}
static inline void ui_rolling_done(lv_anim_t *a){ui_rolling_commit((ui_rolling_text_t *)a->var);}
static inline void ui_rolling_stop(ui_rolling_text_t *r) {
    if(!r->box)return;
    lv_anim_delete(r,ui_rolling_animate);
    if(r->active)ui_rolling_commit(r);
}
static inline void ui_rolling_create(ui_rolling_text_t *r,lv_obj_t *parent,int x,int y,int width,int height,const lv_font_t *font,lv_color_t color) {
    r->height=height;r->active=false;r->initialized=false;r->offset=(height-(int)font->line_height)/2;
    r->box=lv_obj_create(parent);lv_obj_remove_style_all(r->box);
    lv_obj_set_size(r->box,width,height);lv_obj_set_pos(r->box,x,y);
    lv_obj_set_scrollable(r->box,false);
    lv_obj_set_overflow_visible(r->box,false);
    for(int i=0;i<2;++i){
        r->labels[i]=lv_label_create(r->box);lv_obj_set_width(r->labels[i],width);
        lv_obj_set_style_pad_all(r->labels[i],0,0);
        lv_obj_set_style_text_font(r->labels[i],font,0);lv_obj_set_style_text_color(r->labels[i],color,0);
        lv_obj_set_style_text_align(r->labels[i],LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(r->labels[i],LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text(r->labels[i],"");
    }
    ui_rolling_position(r,0);
}
static inline void ui_rolling_set(ui_rolling_text_t *r,const char *text,bool animated) {
    if(!r->box)return;
    if(r->initialized && strcmp(lv_label_get_text(r->labels[r->active?1:0]),text)==0)return;
    const bool initialized=r->initialized;r->initialized=true;
    ui_rolling_stop(r);lv_label_set_text(r->labels[1],text);
    if(!animated||!initialized){ui_rolling_commit(r);return;}
    r->active=true;lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,r);
    lv_anim_set_values(&a,0,r->height);lv_anim_set_duration(&a,280);
    lv_anim_set_exec_cb(&a,ui_rolling_animate);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a,ui_rolling_done);lv_anim_start(&a);
}
