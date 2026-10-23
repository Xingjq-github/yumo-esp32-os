#include "compact_ui.hpp"
#include "theme.hpp"
#include "text_input.hpp"
#include "control_states.h"
#include <cstring>
namespace ui::compact {
void buttonBackground(lv_obj_t *obj,lv_color_t bg) {
    auto ink=theme::fg();
    if(lv_color_eq(bg,theme::accent())) ink=theme::onAccent();
    else if(lv_color_eq(bg,theme::danger()) || lv_color_eq(bg,theme::success()) || lv_color_eq(bg,theme::warning()))
        ink=theme::light()?lv_color_white():theme::bg();
    // Base appearance changes atomically; checked/pressed/disabled ink stays
    // declarative and never captures an intermediate animation/focus color.
    lv_obj_set_style_bg_color(obj,bg,LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj,ink,LV_STATE_DEFAULT);
}
lv_obj_t *panel(lv_obj_t *parent,int width,int height,bool scroll) {
    auto obj=lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,theme::bg(),0); lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(obj,theme::fg(),0);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_pad_row(obj,kGap,0); lv_obj_set_style_pad_column(obj,kGap,0);
    if(scroll) { lv_obj_set_scrollable(obj,true); lv_obj_set_scroll_dir(obj,LV_DIR_VER); }
    else lv_obj_set_scrollable(obj,false);
    lv_obj_set_scrollbar_mode(obj,LV_SCROLLBAR_MODE_AUTO);
    return obj;
}
lv_obj_t *label(lv_obj_t *parent,const char *text,int width) {
    auto obj=lv_label_create(parent); lv_obj_set_style_text_font(obj,theme::fontSmall(),0);
    lv_obj_set_style_pad_all(obj,0,0); // text color inherits panel/button state
    if(width!=LV_SIZE_CONTENT) { lv_obj_set_width(obj,width); lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_DOTS); }
    lv_label_set_text(obj,text); return obj;
}
lv_obj_t *button(lv_obj_t *parent,const char *text,lv_event_cb_t cb,void *context,int width,int height) {
    auto obj=lv_button_create(parent); lv_obj_set_size(obj,width,height);
    lv_obj_set_style_pad_all(obj,kGap,0); lv_obj_set_style_radius(obj,theme::radius(),0);
    lv_obj_set_style_border_width(obj,theme::tiles()?0:1,0); lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_set_style_border_color(obj,theme::edge(),0);
    lv_obj_set_style_bg_color(obj,theme::surface(),0);
    ui_control_ink(obj,theme::fg(),theme::onAccent());
    ui_control_transitions(obj);
    lv_obj_set_style_bg_color(obj,theme::accent(),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj,theme::accent(),LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj,theme::surface(),LV_STATE_DISABLED);
    lv_obj_set_style_opa(obj,LV_OPA_50,LV_STATE_DISABLED);
    lv_obj_set_scrollable(obj,false);
    if(cb) lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,context);
    if(text && *text) { auto title=label(obj,text); lv_obj_center(title); }
    return obj;
}
lv_obj_t *row(lv_obj_t *parent,const char *title,const char *value,lv_event_cb_t cb,void *context,lv_obj_t **valueLabel) {
    auto obj=button(parent,"",cb,context);
    auto name=label(obj,title,value?112:216); lv_obj_align(name,LV_ALIGN_LEFT_MID,0,0);
    if(value) {
        auto detail=label(obj,value,104); lv_obj_set_style_text_align(detail,LV_TEXT_ALIGN_RIGHT,0);
        lv_obj_set_style_text_color(detail,theme::muted(),0); lv_obj_align(detail,LV_ALIGN_RIGHT_MID,-17,0);
        if(valueLabel) *valueLabel=detail;
    }
    auto arrow=label(obj,LV_SYMBOL_RIGHT); lv_obj_align(arrow,LV_ALIGN_RIGHT_MID,0,0);
    return obj;
}
lv_obj_t *toggle(lv_obj_t *parent,const char *title,bool on,lv_event_cb_t cb,void *context) {
    auto obj=button(parent,"",nullptr,nullptr);
    auto name=label(obj,title,174); lv_obj_align(name,LV_ALIGN_LEFT_MID,0,0);
    auto sw=lv_switch_create(obj); lv_obj_set_size(sw,52,30); lv_obj_align(sw,LV_ALIGN_RIGHT_MID,0,0);
    lv_obj_set_style_pad_all(sw,1,0);
    const lv_style_selector_t checkedIndicator=static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
                                               static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw,theme::accent(),checkedIndicator);
    lv_obj_set_style_bg_color(sw,theme::edge(),0);
    lv_obj_set_style_bg_color(sw,theme::fg(),LV_PART_KNOB);
    checked(sw,on); lv_obj_set_clickable(sw,false);
    lv_obj_add_event_cb(sw,cb,LV_EVENT_VALUE_CHANGED,context);
    // The entire 240x40 row, not the small visual switch, is the touch target.
    lv_obj_add_event_cb(obj,[](lv_event_t *event) {
        auto target=static_cast<lv_obj_t *>(lv_event_get_user_data(event));
        if(lv_obj_has_state(target,LV_STATE_DISABLED)) return;
        checked(target,!lv_obj_has_state(target,LV_STATE_CHECKED));
        lv_obj_send_event(target,LV_EVENT_VALUE_CHANGED,nullptr);
    },LV_EVENT_CLICKED,sw);
    return sw;
}
lv_obj_t *section(lv_obj_t *parent,const char *text) {
    auto obj=label(parent,text,LV_PCT(100)); lv_obj_set_height(obj,24);
    lv_obj_set_style_text_color(obj,theme::muted(),0); return obj;
}
lv_obj_t *field(lv_obj_t *parent,const char *placeholder,const char *text,uint32_t maxLength) {
    auto obj=lv_textarea_create(parent); lv_obj_set_size(obj,LV_PCT(100),kTouch);
    lv_obj_set_style_text_font(obj,theme::fontSmall(),0); lv_obj_set_style_text_color(obj,theme::fg(),0);
    lv_obj_set_style_bg_color(obj,theme::surface(),0); lv_obj_set_style_pad_all(obj,1,0);
    lv_obj_set_style_border_width(obj,1,0); lv_obj_set_style_radius(obj,theme::radius(),0);
    lv_obj_set_style_border_color(obj,theme::edge(),0);
    lv_obj_set_style_border_color(obj,theme::accent(),LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(obj,theme::muted(),LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(obj,theme::accent(),LV_PART_CURSOR);
    TextInput::bindField(obj);
    lv_textarea_set_one_line(obj,true); lv_textarea_set_max_length(obj,maxLength);
    lv_textarea_set_placeholder_text(obj,placeholder); lv_textarea_set_text(obj,text);
    lv_obj_set_gesture_bubble(obj,false);
    return obj;
}
lv_obj_t *networkField(lv_obj_t *parent,const char *placeholder,const char *text,uint32_t maxLength,const lv_font_t *font) {
    auto obj=field(parent,placeholder,text,maxLength);
    if(font) lv_obj_set_style_text_font(obj,font,0);
    // One-line mode resets the content height; apply the shared touch height last.
    constexpr int height=TextInputLayout::NetworkFieldHeight;
    lv_obj_set_height(obj,height);
    font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
    const int pad=(height-int(font->line_height)-2)/2;
    lv_obj_set_style_pad_top(obj,pad>0?pad:0,0);
    lv_obj_set_style_pad_bottom(obj,pad>0?pad:0,0);
    return obj;
}
lv_obj_t *dropdown(lv_obj_t *parent,const char *options,int width) {
    auto obj=lv_dropdown_create(parent); lv_obj_set_size(obj,width,kTouch);
    ui_control_transitions(obj);
    lv_obj_set_style_pad_all(obj,1,0); lv_obj_set_style_radius(obj,theme::radius(),0);
    lv_obj_set_style_text_font(obj,theme::fontSmall(),0); lv_obj_set_style_text_color(obj,theme::fg(),0);
    lv_obj_set_style_bg_color(obj,theme::surface(),0); lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_border_color(obj,theme::edge(),0);
    lv_dropdown_set_options(obj,options); lv_dropdown_set_dir(obj,LV_DIR_TOP);
    auto list=lv_dropdown_get_list(obj);
    lv_obj_set_style_text_font(list,theme::fontSmall(),0); lv_obj_set_style_text_color(list,theme::fg(),0);
    lv_obj_set_style_bg_color(list,theme::surface(),0); lv_obj_set_style_max_height(list,144,0);
    const auto selected=static_cast<lv_style_selector_t>(LV_PART_SELECTED)|static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(list,theme::accent(),selected);lv_obj_set_style_text_color(list,theme::onAccent(),selected);
    lv_obj_set_style_bg_color(list,theme::accent(),LV_PART_SELECTED);lv_obj_set_style_text_color(list,theme::onAccent(),LV_PART_SELECTED);
    // Dropdown options need larger per-option targets than ordinary text.
    lv_obj_set_style_text_line_space(list,14,0); lv_obj_set_style_pad_all(list,1,0);
    return obj;
}
void setText(lv_obj_t *obj,const char *text) { if(obj && std::strcmp(lv_label_get_text(obj),text)!=0) lv_label_set_text(obj,text); }
void checked(lv_obj_t *obj,bool value) { if(!obj) return; if(value) lv_obj_add_state(obj,LV_STATE_CHECKED); else lv_obj_remove_state(obj,LV_STATE_CHECKED); }
void disabled(lv_obj_t *obj,bool value) { if(!obj) return; if(value) lv_obj_add_state(obj,LV_STATE_DISABLED); else lv_obj_remove_state(obj,LV_STATE_DISABLED); }
} // namespace ui::compact
