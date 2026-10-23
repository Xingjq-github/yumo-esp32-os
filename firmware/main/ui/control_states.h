// Shared LVGL state policy. C-compatible so the actual LVGL transitions can be
// replayed on a host as well as on the device. No callbacks mutate state styles.
#pragma once
#include "lvgl.h"

static inline void ui_control_transitions(lv_obj_t *obj)
{
    // An EMPTY property list does not override the default theme's transitions:
    // LVGL merges properties from all matching styles. Override each property.
    static const lv_style_prop_t props[] = {
        LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR,
        LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
        LV_STYLE_TRANSLATE_Y, LV_STYLE_TRANSLATE_X, LV_STYLE_TRANSFORM_ROTATION,
        LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y,
        LV_STYLE_RECOLOR_OPA, LV_STYLE_RECOLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_OPA,
        LV_STYLE_PROP_INV
    };
    static const lv_style_transition_dsc_t instant = {props, NULL, lv_anim_path_linear, 0, 0};
    lv_obj_set_style_transition(obj, &instant, LV_STATE_DEFAULT);
    lv_obj_set_style_transition(obj, &instant, LV_STATE_PRESSED);
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_STATE_PRESSED);
}

static inline void ui_control_ink(lv_obj_t *obj, lv_color_t normal, lv_color_t selected)
{
    lv_obj_set_style_text_color(obj, normal, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, selected, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj, selected, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, normal, LV_STATE_DISABLED);
}
