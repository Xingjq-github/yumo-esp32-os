#include "boot_animation.hpp"
#include "boot_animation_model.hpp"
#include "theme.hpp"
#include "lvgl.h"
namespace ui {
namespace {
lv_obj_t *cover=nullptr,*strokes[3]{},*dots[4]{},*wordmark=nullptr,*bar=nullptr;
lv_point_precise_t points[3][2]{};
lv_timer_t *timer=nullptr;
uint32_t started=0;
bool played=false;
lv_obj_t *rectangle(int x,int y,int width,int height,lv_color_t color) {
    auto obj=lv_obj_create(cover);lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,color,0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_clickable(obj,false);
    lv_obj_set_scrollable(obj,false);
    return obj;
}
void tick(lv_timer_t *) {
    const auto elapsed=lv_tick_get()-started;
    if(elapsed>=boot::DurationMs) { BootAnimation::stop(); return; }
    const auto frame=boot::frame(elapsed);
    // Draw the Y from three short lines, never rotate a screen-sized bitmap.
    const int x[]={96,144,120},y[]={60,60,86},endY[]={86,86,112};
    for(int i=0;i<3;++i) {
        const unsigned progress=i<2?frame.branches:frame.stem;
        points[i][0]={lv_value_precise_t(x[i]),lv_value_precise_t(y[i])};
        points[i][1]={lv_value_precise_t(x[i]+(120-x[i])*int(progress)/255),
                      lv_value_precise_t(y[i]+(endY[i]-y[i])*int(progress)/255)};
        lv_line_set_points(strokes[i],points[i],2);
        lv_obj_set_style_line_opa(strokes[i],progress?LV_OPA_COVER:LV_OPA_TRANSP,0);
    }
    for(int i=0;i<4;++i) {
        const int angle=(frame.angle+i*90)%360;
        const int x=120+(lv_trigo_cos(angle)*frame.radius)/32767-3;
        const int y=86+(lv_trigo_sin(angle)*frame.radius)/32767-3;
        lv_obj_set_pos(dots[i],x,y);
        lv_obj_set_style_bg_opa(dots[i],frame.dotOpacity,0);
    }
    lv_obj_set_style_text_opa(wordmark,frame.textOpacity,0);
    lv_obj_align(wordmark,LV_ALIGN_TOP_MID,0,frame.textY);
    lv_obj_set_width(bar,frame.barWidth);
}
}
void BootAnimation::start() {
    if(played) return;
    played=true; cover=lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover,240,240); lv_obj_set_style_bg_color(cover,theme::bg(),0);
    lv_obj_set_style_bg_opa(cover,LV_OPA_COVER,0); lv_obj_set_scrollable(cover,false);
    lv_obj_set_clickable(cover,true); // no accidental app launch during boot
    for(auto &stroke:strokes) {
        stroke=lv_line_create(cover);lv_obj_remove_style_all(stroke);
        lv_obj_align(stroke,LV_ALIGN_TOP_LEFT,0,0);
        lv_obj_set_style_line_width(stroke,6,0);
        lv_obj_set_style_line_rounded(stroke,true,0);
        lv_obj_set_style_line_color(stroke,theme::accent(),0);
        lv_obj_set_clickable(stroke,false);
    }
    for(int i=0;i<4;++i) {
        dots[i]=rectangle(0,0,6,6,theme::accent());
        lv_obj_set_style_radius(dots[i],LV_RADIUS_CIRCLE,0);
    }
    wordmark=lv_label_create(cover);lv_obj_remove_style_all(wordmark);
    lv_obj_set_size(wordmark,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
    lv_label_set_text(wordmark,"YUMO");
    lv_obj_set_style_text_font(wordmark,&lv_font_montserrat_32,0);
    lv_obj_set_style_text_letter_space(wordmark,5,0);
    lv_obj_set_style_text_color(wordmark,theme::fg(),0);
    rectangle(60,196,120,2,theme::edge());
    bar=rectangle(60,196,0,2,theme::accent());
    started=lv_tick_get();tick(nullptr);timer=lv_timer_create(tick,boot::FrameMs,nullptr);
    if(!timer) stop(); // Never leave the input-blocking cover up on allocation failure.
}
void BootAnimation::stop() {
    if(timer) { lv_timer_delete(timer); timer=nullptr; }
    if(cover) {
        lv_obj_delete(cover); cover=nullptr;
        lv_obj_invalidate(lv_screen_active());
    }
    for(auto &stroke:strokes) stroke=nullptr;
    for(auto &dot:dots) dot=nullptr;
    wordmark=bar=nullptr;
}
} // namespace ui
