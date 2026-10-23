#pragma once
#include "compact_ui.hpp"
#include "theme.hpp"

// Shared HID control states follow the active theme with explicit state colors.
namespace ui::hid {
inline void style(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj,theme::surface(),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(obj,theme::accent(),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj,theme::accent(),LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj,theme::surface(),LV_STATE_DISABLED);
    lv_obj_set_style_text_color(obj,theme::fg(),0);
    lv_obj_set_style_text_color(obj,theme::onAccent(),LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj,theme::onAccent(),LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj,theme::fg(),LV_STATE_DISABLED);
    lv_obj_set_style_border_width(obj,theme::tiles()?0:1,0);
    lv_obj_set_style_border_color(obj,theme::edge(),0);
    lv_obj_set_style_radius(obj,theme::radius(),0);
    lv_obj_set_style_pad_all(obj,1,0);
    lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_set_style_transform_width(obj,0,LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj,0,LV_STATE_PRESSED);
}
inline lv_obj_t *button(lv_obj_t *parent,const char *text,lv_event_cb_t cb,void *context,
                        int x,int y,int w,int h) {
    auto obj=compact::button(parent,text,cb,context,w,h);
    style(obj); lv_obj_set_pos(obj,x,y);
    return obj;
}
inline lv_obj_t *caption(lv_obj_t *button) { return lv_obj_get_child(button,0); }
} // namespace ui::hid
