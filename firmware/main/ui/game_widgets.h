// Small in-scene game controls. C-compatible for native LVGL interaction replay.
#pragma once
#include "lvgl.h"
#include "control_states.h"
LV_FONT_DECLARE(lv_font_menu_14);

static inline const lv_font_t *game_font(void){
    // The compact CJK subset does not contain every number or LVGL symbol.
    // Keep its Chinese glyphs and resolve dynamic scores/arrows through the
    // already linked Latin/symbol face. Initialized on the LVGL task only.
    static lv_font_t font;
    if(!font.get_glyph_dsc){font=lv_font_menu_14;font.fallback=&lv_font_montserrat_14;}
    return &font;
}

static inline lv_obj_t *game_box(lv_obj_t *parent,int x,int y,int w,int h,lv_color_t color,int radius){
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_bg_color(o,color,0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    do { lv_obj_set_clickable(o,false); lv_obj_set_scrollable(o,false); lv_obj_set_event_bubble(o,false); lv_obj_set_gesture_bubble(o,false); } while(0);
    return o;
}
static inline lv_obj_t *game_overlay(lv_obj_t *scene){
    lv_obj_t *o=game_box(scene,0,0,240,240,lv_color_black(),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
    // Catch touches outside the controls; never pass a menu tap to the game.
    lv_obj_set_clickable(o,true);return o;
}
static inline lv_obj_t *game_text(lv_obj_t *parent,const char *text,int x,int y,int width,lv_color_t ink){
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,game_font(),0);lv_obj_set_style_text_color(o,ink,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_label_set_text(o,text);
    do { lv_obj_set_clickable(o,false); lv_obj_set_event_bubble(o,false); lv_obj_set_gesture_bubble(o,false); } while(0);
    return o;
}
static inline void game_button_colors(lv_obj_t *o,lv_color_t bg,lv_color_t ink,lv_color_t edge){
    lv_obj_set_style_bg_color(o,bg,LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o,lv_color_mix(ink,bg,40),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(o,edge,0);ui_control_ink(o,ink,ink);
}
static inline lv_obj_t *game_button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,
                                   lv_color_t bg,lv_color_t ink,lv_color_t edge,int radius,
                                   lv_event_cb_t cb,void *context){
    lv_obj_t *o=lv_button_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,2,0);
    lv_obj_set_style_opa(o,LV_OPA_50,LV_STATE_DISABLED);
    do { lv_obj_set_scrollable(o,false); lv_obj_set_event_bubble(o,false); lv_obj_set_gesture_bubble(o,false); } while(0);
    ui_control_transitions(o);game_button_colors(o,bg,ink,edge);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,context);
    if(text&&*text){
        lv_obj_t *label=lv_label_create(o);lv_obj_set_style_text_font(label,game_font(),0);
        lv_label_set_text(label,text);lv_obj_center(label);
        do { lv_obj_set_clickable(label,false); lv_obj_set_event_bubble(label,false); lv_obj_set_gesture_bubble(label,false); } while(0);
    }
    return o;
}
static inline void game_dropdown_colors(lv_obj_t *o,lv_color_t bg,lv_color_t ink,lv_color_t edge,lv_color_t selected){
    lv_obj_set_style_radius(o,8,0);lv_obj_set_style_border_width(o,2,0);
    lv_obj_set_style_bg_color(o,bg,0);lv_obj_set_style_border_color(o,edge,0);
    lv_obj_set_style_text_font(o,game_font(),0);lv_obj_set_style_text_color(o,ink,0);
    lv_obj_t *list=lv_dropdown_get_list(o);ui_control_transitions(list);
    lv_obj_set_style_radius(list,8,0);lv_obj_set_style_bg_color(list,bg,0);
    lv_obj_set_style_border_color(list,edge,0);lv_obj_set_style_text_color(list,ink,0);
    lv_obj_set_style_text_font(list,game_font(),0);
    lv_obj_set_style_bg_color(list,selected,LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list,selected,lv_style_selector_t(LV_PART_SELECTED)|lv_style_selector_t(LV_STATE_CHECKED));
    lv_obj_set_style_text_color(list,ink,LV_PART_SELECTED);
    lv_obj_set_style_text_color(list,ink,lv_style_selector_t(LV_PART_SELECTED)|lv_style_selector_t(LV_STATE_CHECKED));
}
static inline lv_obj_t *game_smile_ball(lv_obj_t *parent,int x,int y,int size){
    const lv_color_t ink=lv_color_hex(0x674221);
    lv_obj_t *o=game_box(parent,x,y,size,size,lv_color_hex(0xFFD365),size/2);
    lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(0xD49636),0);
    const int eye=size>=24?3:2,high=size>=24?5:2;
    game_box(o,size/3-1,size/3-1,eye,high,ink,1);
    game_box(o,size*2/3-1,size/3-1,eye,high,ink,1);
    const int mouthX=size/3,mouthY=size*2/3-1,mouthW=size/3;
    game_box(o,mouthX,mouthY,eye,eye+1,ink,1);
    game_box(o,mouthX+mouthW-eye,mouthY,eye,eye+1,ink,1);
    game_box(o,mouthX+1,mouthY+eye,mouthW-2,eye,ink,eye);
    game_box(o,size/5-1,size/7-1,size/4,size>=24?3:1,lv_color_hex(0xFFF5CB),2);
    return o;
}
