/* Actual production state policy + actual LVGL 9.5 default theme. Host-only.
 * No device I/O. The runner supplies the five production palette values. */
#include "ui/control_states.h"
#include "src/core/lv_obj_private.h"
#include "palettes.h"
#include <assert.h>
#include <stdio.h>

static void advance(unsigned ms) {lv_tick_inc(ms);lv_anim_refr_now();lv_timer_handler();}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){(void)a;(void)p;lv_display_flush_ready(d);}

/* Preserve the previous callback here solely to demonstrate the regression.
 * It reads the animated background, then persists ink for a whole state mask. */
static lv_color_t old_fg,old_accent,old_selected;
static void old_ink(lv_event_t *event){
    lv_obj_t *obj=lv_event_get_target_obj(event);
    lv_color_t bg=lv_obj_get_style_bg_color(obj,LV_PART_MAIN);
    lv_color_t ink=lv_color_eq(bg,old_accent)?old_selected:old_fg;
    if(!lv_color_eq(ink,lv_obj_get_style_text_color(obj,LV_PART_MAIN)))
        lv_obj_set_style_text_color(obj,ink,(lv_style_selector_t)lv_obj_get_state(obj));
}
static lv_obj_t *make_button(lv_obj_t *parent,const unsigned *p,int old){
    lv_obj_t *obj=lv_button_create(parent);lv_obj_set_size(obj,210,40);
    lv_obj_set_style_bg_color(obj,lv_color_hex(p[1]),0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(p[3]),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj,lv_color_hex(p[3]),LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj,lv_color_hex(p[1]),LV_STATE_DISABLED);
    lv_obj_set_style_opa(obj,LV_OPA_50,LV_STATE_DISABLED);
    if(old){
        static const lv_style_prop_t empty[]={LV_STYLE_PROP_INV};
        static const lv_style_transition_dsc_t noop={empty,NULL,NULL,0,0};
        lv_obj_set_style_text_color(obj,old_fg,0);
        lv_obj_set_style_transition(obj,&noop,0);lv_obj_set_style_transition(obj,&noop,LV_STATE_PRESSED);
        lv_obj_set_style_recolor_opa(obj,LV_OPA_TRANSP,LV_STATE_PRESSED);
        lv_obj_add_event_cb(obj,old_ink,LV_EVENT_STYLE_CHANGED,NULL);
        lv_obj_add_event_cb(obj,old_ink,LV_EVENT_STATE_CHANGED,NULL);
    }else{
        ui_control_ink(obj,lv_color_hex(p[2]),lv_color_hex(p[6]));
        ui_control_transitions(obj);
    }
    lv_label_set_text(lv_label_create(obj),"APP / OPTION");
    return obj;
}
static unsigned checks;
static void check(lv_obj_t *obj,const unsigned *p){
    lv_state_t state=lv_obj_get_state(obj);
    int disabled=(state&LV_STATE_DISABLED)!=0;
    int selected=!disabled&&(state&(LV_STATE_CHECKED|LV_STATE_PRESSED));
    lv_color_t expected=lv_color_hex(p[selected?6:2]);
    if(!lv_color_eq(lv_obj_get_style_text_color(obj,LV_PART_MAIN),expected))
        printf("ink mismatch state=%u checks=%u\n",(unsigned)state,checks);
    assert(lv_color_eq(lv_obj_get_style_text_color(obj,LV_PART_MAIN),expected));
    assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(obj,0),LV_PART_MAIN),expected));
    assert(lv_color_eq(lv_obj_get_style_bg_color(obj,LV_PART_MAIN),lv_color_hex(p[selected?3:1])));
    assert(lv_obj_get_style_opa(obj,LV_PART_MAIN)==(disabled?LV_OPA_50:LV_OPA_COVER));
    if(!disabled)assert(lv_obj_get_style_recolor_opa(obj,LV_PART_MAIN)==LV_OPA_TRANSP);
    ++checks;
}
int main(void){
    setbuf(stdout,NULL);puts("init");lv_init();puts("display");lv_display_t *display=lv_display_create(240,240);
    static uint16_t buffer[240*40];
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    // Render each newly created tree explicitly. State replay does not need to
    // redraw 20,000 identical pixel buffers, but objects remain truly rendered.
    lv_timer_pause(lv_display_get_refr_timer(display));
    unsigned reproduced=0;
    for(unsigned theme=0;theme<5;++theme){
        printf("theme %u\n",theme);
        const unsigned *p=palettes[theme];
        old_fg=lv_color_hex(p[2]);old_accent=lv_color_hex(p[3]);old_selected=lv_color_hex(p[6]);
        lv_display_set_theme(display,lv_theme_default_init(display,old_accent,old_accent,theme==1||theme==4,&lv_font_montserrat_14));
        lv_obj_t *root=lv_obj_create(lv_screen_active());lv_obj_set_size(root,240,240);
        lv_obj_t *old=make_button(root,p,1);puts("old render");lv_refr_now(display);puts("old press");
        lv_obj_add_state(old,LV_STATE_PRESSED);advance(90);
        lv_obj_remove_state(old,LV_STATE_PRESSED);lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);advance(200);
        if(!lv_color_eq(lv_obj_get_style_text_color(old,LV_PART_MAIN),old_fg))++reproduced;
        lv_obj_delete(old);lv_obj_remove_flag(root,LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *obj=make_button(root,p,0);puts("new render");lv_refr_now(display);unsigned count=0;
        for(unsigned repeat=0;repeat<500;++repeat){
            lv_obj_add_state(obj,LV_STATE_FOCUSED);advance(20);check(obj,p);
            lv_obj_add_state(obj,LV_STATE_PRESSED);advance(20);check(obj,p);
            lv_obj_remove_state(obj,LV_STATE_PRESSED);
            lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);advance(200);check(obj,p);
            lv_obj_remove_flag(root,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(obj,LV_STATE_CHECKED);advance(20);check(obj,p);
            lv_obj_add_state(obj,LV_STATE_PRESSED|LV_STATE_DISABLED);advance(20);check(obj,p);
            lv_obj_remove_state(obj,LV_STATE_DISABLED|LV_STATE_PRESSED);advance(20);check(obj,p);
            lv_obj_remove_state(obj,LV_STATE_CHECKED|LV_STATE_FOCUSED);advance(100);check(obj,p);
            // Scrolling cancels press without a click; then touch focus changes.
            lv_obj_add_state(obj,LV_STATE_PRESSED|LV_STATE_FOCUS_KEY);advance(5);
            lv_obj_remove_state(obj,LV_STATE_PRESSED|LV_STATE_FOCUS_KEY);advance(100);check(obj,p);
            if(!repeat)count=obj->style_cnt;
            assert(count==obj->style_cnt); // no accumulated per-combination styles
        }
        lv_obj_delete(root);
    }
    assert(reproduced==5);
    printf("old_failure_themes=%u; new_state_checks=%u; retained_styles=constant\n",reproduced,checks);
    lv_deinit();return 0;
}
