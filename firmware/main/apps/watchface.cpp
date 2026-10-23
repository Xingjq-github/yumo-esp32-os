#include "watchface.hpp"

#include "theme.hpp"
#include "ui/compact_ui.hpp"
#include "core/power_mgr.hpp"

#include <cmath>
#include <stdio.h>
#include <time.h>

LV_FONT_DECLARE(lv_font_clock_84);

namespace apps {

static constexpr int kFaceCount = 4;
static constexpr int kFaceHeight=240;
static constexpr int kDialSize = kFaceHeight;
static constexpr int kDialCenter = kDialSize / 2;
static constexpr double kPi = 3.14159265358979323846;

static void setVisible(lv_obj_t *obj, bool visible)
{
    if (visible) lv_obj_set_hidden(obj,false);
    else lv_obj_set_hidden(obj,true);
}

// Gestures originate from whichever child object was actually touched. To keep
// both digital and analog faces switchable, the GESTURE_BUBBLE flag must be
// applied recursively to the whole subtree (dial ticks, hands, hub, labels),
// not just the parent's direct children.
static void enableGestureBubbleRecursive(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_gesture_bubble(obj,true);
    lv_obj_set_event_bubble(obj,true);
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
        enableGestureBubbleRecursive(lv_obj_get_child(obj, static_cast<int32_t>(i)));
    }
}

static lv_obj_t *createHand(lv_obj_t *dial, lv_point_precise_t points[2],
                            int width, lv_color_t color)
{
    lv_obj_t *line = lv_line_create(dial);
    lv_obj_remove_style_all(line);
    lv_obj_align(line,LV_ALIGN_TOP_LEFT,0,0);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static void setHand(lv_obj_t *hand, lv_point_precise_t points[2],
                    double degrees, int length, int tail = 0, int center=kDialCenter)
{
    const double radians = (degrees - 90.0) * kPi / 180.0;
    const double x = std::cos(radians);
    const double y = std::sin(radians);
    points[0].x = center - static_cast<lv_coord_t>(x * tail);
    points[0].y = center - static_cast<lv_coord_t>(y * tail);
    points[1].x = center + static_cast<lv_coord_t>(x * length);
    points[1].y = center + static_cast<lv_coord_t>(y * length);
    lv_line_set_points(hand, points, 2);
}

const char *Watchface::icon(void) const
{
    return LV_SYMBOL_BELL;
}

void Watchface::onCreate(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(parent,false);
    lv_obj_set_clickable(parent,true);

    lv_obj_set_style_pad_all(parent,0,0);
    lv_obj_set_style_border_width(parent,0,0);
    m_faceArea=ui::compact::panel(parent,240,kFaceHeight);lv_obj_set_pos(m_faceArea,0,0);
    lv_obj_set_style_bg_opa(m_faceArea,LV_OPA_TRANSP,0);
    for(auto &block:m_blocks) {
        block=ui::compact::panel(m_faceArea,1,1);
        lv_obj_set_clickable(block,false);
    }
    // Edge-to-edge digital face, no inset card. Fixed digit cells avoid jumps
    // between 1 and 8 and keep the normal layout stable as the time changes.
    m_weekday=ui::compact::label(m_faceArea,"THU");
    lv_obj_set_style_text_font(m_weekday,ui::theme::fontLarge(),0);
    lv_obj_set_style_text_color(m_weekday,ui::theme::accent(),0);
    m_date=ui::compact::label(m_faceArea,"2026.09.17");
    lv_obj_set_style_text_font(m_date,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(m_date,ui::theme::muted(),0);
    for(auto &digit:m_digits) {
        digit=ui::compact::label(m_faceArea,"0",48);
        lv_obj_set_style_text_font(digit,&lv_font_clock_84,0);
        lv_obj_set_style_text_align(digit,LV_TEXT_ALIGN_CENTER,0);
    }
    m_colon=ui::compact::label(m_faceArea,":");
    lv_obj_set_style_text_font(m_colon,&lv_font_clock_84,0);
    lv_obj_set_style_text_color(m_colon,ui::theme::accent(),0);
    m_seconds=ui::compact::label(m_faceArea,"00 SEC");
    lv_obj_set_style_text_font(m_seconds,ui::theme::fontLarge(),0);
    lv_obj_set_style_text_color(m_seconds,ui::theme::muted(),0);
    m_dial=ui::compact::panel(m_faceArea,kDialSize,kDialSize);
    lv_obj_align(m_dial,LV_ALIGN_TOP_LEFT,0,0);
    lv_obj_set_style_radius(m_dial,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(m_dial,ui::theme::bg(),0);
    lv_obj_set_style_border_width(m_dial,0,0);
    lv_obj_set_style_border_color(m_dial,ui::theme::edge(),0);
    // Persistent vector ticks; no per-frame allocation or rotating container.
    for(int i=0;i<60;++i) {
        const double radians=(i*6.0-90.0)*kPi/180.0;
        const int outer=kDialCenter-2,inner=outer-(i%5==0?9:3);
        auto &points=m_tickPoints[i];
        points[0]={static_cast<lv_value_precise_t>(kDialCenter+std::cos(radians)*inner),
                   static_cast<lv_value_precise_t>(kDialCenter+std::sin(radians)*inner)};
        points[1]={static_cast<lv_value_precise_t>(kDialCenter+std::cos(radians)*outer),
                   static_cast<lv_value_precise_t>(kDialCenter+std::sin(radians)*outer)};
        m_tickLines[i]=createHand(m_dial,points,i%5==0?3:1,i%5==0?ui::theme::fg():ui::theme::edge());
    }
    const char *hours[]={"12","3","6","9"};
    const int nx[]={0,90,0,-90},ny[]={-89,0,89,0};
    for(unsigned i=0;i<4;++i) {
        m_numerals[i]=ui::compact::label(m_dial,hours[i]);
        lv_obj_set_style_text_font(m_numerals[i],ui::theme::fontLarge(),0);
        lv_obj_set_style_text_color(m_numerals[i],ui::theme::fg(),0);
        lv_obj_align(m_numerals[i],LV_ALIGN_CENTER,nx[i],ny[i]);
    }
    m_dialBrand=ui::compact::label(m_dial,"CLASSIC");
    lv_obj_set_style_text_font(m_dialBrand,&lv_font_montserrat_10,0);
    lv_obj_set_style_text_letter_space(m_dialBrand,2,0);
    lv_obj_set_style_text_color(m_dialBrand,ui::theme::accent(),0);
    lv_obj_align(m_dialBrand,LV_ALIGN_CENTER,0,-46);
    m_centerDate=ui::compact::label(m_faceArea,"");
    lv_obj_set_style_text_font(m_centerDate,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(m_centerDate,ui::theme::muted(),0);
    m_centerTime=ui::compact::label(m_faceArea,"");
    lv_obj_set_style_text_font(m_centerTime,ui::theme::fontLarge(),0);
    lv_obj_set_style_text_color(m_centerTime,ui::theme::accent(),0);
    lv_obj_align(m_centerTime,LV_ALIGN_CENTER,0,42);
    // Hands above the complications, not obscured by opaque label rectangles.
    m_hourHand=createHand(m_dial,m_hourPoints,7,ui::theme::fg());
    m_minuteHand=createHand(m_dial,m_minutePoints,4,ui::theme::fg());
    m_secondHand=createHand(m_dial,m_secondPoints,2,ui::theme::accent());
    auto hub=ui::compact::panel(m_dial,10,10);lv_obj_center(hub);
    lv_obj_set_style_radius(hub,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(hub,ui::theme::fg(),0);
    for(auto &caption:m_captions) {
        caption=ui::compact::label(m_faceArea,"");
        lv_obj_set_style_text_font(caption,&lv_font_montserrat_10,0);
        lv_obj_set_style_text_color(caption,ui::theme::muted(),0);
    }

    // Gestures are emitted by the touched child object. Make every watch-face
    // child propagate them to the app root so both digital and analog faces
    // keep responding without adding an input-blocking overlay.
    const uint32_t childCount = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < childCount; ++i) {
        enableGestureBubbleRecursive(lv_obj_get_child(parent, static_cast<int32_t>(i)));
    }
    lv_obj_add_event_cb(parent, touchCb, LV_EVENT_GESTURE, this);
    // Fallback: manual swipe tracking on press/release in case the LVGL indev
    // gesture recognizer never fires a LV_EVENT_GESTURE for this window.
    lv_obj_add_event_cb(parent, pressCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(parent, pressCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(parent, pressCb, LV_EVENT_PRESS_LOST, this);

    applyFace();

    m_timer = lv_timer_create(tickCb, 200, this);
    refresh();
}

void Watchface::onShow() { m_visible=true;if(m_timer) lv_timer_resume(m_timer);m_lastRendered=static_cast<time_t>(-1);refresh(); }
void Watchface::onPowerSaving(bool enabled) {
    m_powerSaving=enabled;
    if(m_timer)lv_timer_set_period(m_timer,enabled?1000:200);
    if(enabled)m_almanac.stop();
}
void Watchface::onHide() { m_visible=false;m_tracking=false;m_almanac.stop();if(m_timer) lv_timer_pause(m_timer); }

void Watchface::onDestroy(void)
{
    m_almanac.stop();
    if (m_timer) {
        lv_timer_delete(m_timer);
        m_timer = nullptr;
    }
}

void Watchface::tickCb(lv_timer_t *t)
{
    Watchface *self = static_cast<Watchface *>(lv_timer_get_user_data(t));
    if(!sys::Power::isScreenOn()) {self->m_almanac.stop();return;} // no hidden redraws
    if(!self->m_visible) return;
    self->refresh();
}

void Watchface::touchCb(lv_event_t *e)
{
    Watchface *self = static_cast<Watchface *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    if (!self || !indev) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) {
        self->m_tracking=false; // a recognized swipe must not switch twice on release
        self->changeFace(direction == LV_DIR_LEFT ? 1 : -1);
        lv_event_stop_bubbling(e);
    }
}

void Watchface::pressCb(lv_event_t *e)
{
    Watchface *self = static_cast<Watchface *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    if (!self || !indev) return;

    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        self->m_pressStartX = (lv_coord_t)p.x;
        self->m_pressStartY = (lv_coord_t)p.y;
        self->m_tracking = true;
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        self->m_tracking = false;
        return;
    }

    // LV_EVENT_RELEASED: decide swipe direction manually as a robust fallback
    // for the LVGL indev gesture recognizer that never fires here.
    if (!self->m_tracking) return;
    self->m_tracking = false;

    const int dx = (int)p.x - (int)self->m_pressStartX;
    const int dy = (int)p.y - (int)self->m_pressStartY;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    if (adx >= 30 && adx > ady) {
        self->changeFace(dx < 0 ? 1 : -1);
    }
}

void Watchface::layoutDigitalFace()
{
    // Fixed positions for each theme's normal digital face.
    lv_obj_t *targets[]={m_weekday,m_date,m_digits[0],m_digits[1],m_colon,m_digits[2],m_digits[3],m_seconds};
    // Mono: fixed-width date/weekday/seconds cells keep the preview's left and
    // right edges stable as their text changes.
    constexpr int xs[]={76,-41,-82,-34,0,34,82,79};
    constexpr int ys[]={46,46,-13,-13,-13,-13,-13,89};
    constexpr int tileX[]={73,73,-74,-26,0,35,83,73};
    constexpr int tileY[]={-16,13,-31,-31,0,80,80,-86};
    constexpr int terminalX[]={80,52,-85,-35,0,35,85,88};
    constexpr int terminalY[]={57,-67,-4,-4,-4,-4,-4,92};
    constexpr int warmX[]={-76,0,-90,-30,0,30,90,81};
    constexpr int warmY[]={88,-65,10,10,0,10,10,88};
    constexpr int focusX[]={-80,-26,-82,-32,0,33,83,-80};
    constexpr int focusY[]={-99,106,-26,-26,0,45,45,63};
    for(unsigned i=0;i<8;++i) {
        auto obj=targets[i];if(!obj) continue;
        const int ox=ui::theme::warm()?warmX[i]:ui::theme::focus()?focusX[i]:ui::theme::tiles()?tileX[i]:ui::theme::terminal()?terminalX[i]:xs[i];
        const int oy=ui::theme::warm()?warmY[i]:ui::theme::focus()?focusY[i]:ui::theme::tiles()?tileY[i]:ui::theme::terminal()?terminalY[i]:ys[i];
        lv_obj_align(obj,LV_ALIGN_CENTER,ox,oy);
    }
    // Decorative rectangles share the same fixed theme layout as the labels.
    struct Rect {int x,y,w,h;};
    const Rect mono[]={{0,0,240,240},{0,37,240,1},{0,186,240,1},{0,0,0,0}};
    const Rect tiles[]={{0,0,143,158},{147,0,93,76},{147,80,93,78},{0,162,240,78}};
    const Rect terminal[]={{0,81,240,70},{5,203,45,27},{5,27,230,1},{0,0,0,0}};
    const Rect warm[]={{2,83,56,90},{62,83,56,90},{122,83,56,90},{182,83,56,90}};
    const Rect focus[]={{8,39,224,1},{116,69,2,115},{8,207,224,1},{0,0,0,0}};
    for(unsigned i=0;i<4;++i) {
        const auto r=ui::theme::warm()?warm[i]:ui::theme::focus()?focus[i]:ui::theme::tiles()?tiles[i]:ui::theme::terminal()?terminal[i]:mono[i];
        lv_obj_set_pos(m_blocks[i],r.x,r.y);lv_obj_set_size(m_blocks[i],r.w,r.h);
        setVisible(m_blocks[i],r.w>0);
    }
    if(ui::theme::tiles()) {
        // The live year/month stays in its fixed 100x15 cell at (10,212).
        lv_obj_align(m_captions[3],LV_ALIGN_CENTER,-60,99);
    }
}

void Watchface::changeFace(int delta)
{
    m_face = (m_face + delta + kFaceCount) % kFaceCount;
    applyFace();
    m_lastRendered=static_cast<time_t>(-1);
    refresh();
}

bool Watchface::onDirectionalKey(lv_dir_t direction) {
    if(!m_visible || (direction!=LV_DIR_LEFT && direction!=LV_DIR_RIGHT)) return false;
    m_tracking=false;
    changeFace(direction==LV_DIR_RIGHT?1:-1);
    return true;
}

void Watchface::applyFace(void)
{
    if(m_face==3) {
        for(uint32_t i=0;i<lv_obj_get_child_count(m_faceArea);++i)setVisible(lv_obj_get_child(m_faceArea,i),false);
        m_almanac.show(m_faceArea);
        enableGestureBubbleRecursive(m_faceArea);
        return;
    }
    m_almanac.hide();
    const bool digital = m_face == 0;
    const bool hybrid = m_face == 2;
    setVisible(m_weekday,digital);setVisible(m_seconds,digital);setVisible(m_colon,digital);
    for(auto digit:m_digits) setVisible(digit,digital);
    setVisible(m_date,digital);setVisible(m_dial,!digital);
    for(auto numeral:m_numerals) setVisible(numeral,!hybrid);
    setVisible(m_centerTime,hybrid);
    setVisible(m_centerDate,!digital);
    layoutFace();
}

void Watchface::layoutFace() {
    namespace t=ui::theme;
    const bool digital=m_face==0,hybrid=m_face==2;
    const bool warmHybrid=t::warm()&&hybrid;
    const bool smallHybrid=hybrid&&(t::warm()||t::tiles());
    for(auto block:m_blocks) {
        setVisible(block,false);lv_obj_set_style_radius(block,t::radius(),0);
        lv_obj_set_style_border_width(block,0,0);lv_obj_set_style_bg_opa(block,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(block,t::surface(),0);
    }
    for(auto caption:m_captions) {
        setVisible(caption,false);ui::compact::setText(caption,"");
        lv_obj_set_style_text_font(caption,&lv_font_montserrat_10,0);
        lv_obj_set_width(caption,LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_LEFT,0);
    }
    for(auto label:{m_weekday,m_date,m_seconds}) {
        lv_obj_set_width(label,LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
    }
    lv_obj_set_width(m_colon,LV_SIZE_CONTENT);
    auto box=[&](unsigned i,int x,int y,int w,int h,bool accent=false) {
        auto block=m_blocks[i];setVisible(block,true);lv_obj_set_pos(block,x,y);lv_obj_set_size(block,w,h);
        lv_obj_set_style_bg_color(block,accent?t::accent():t::surface(),0);
    };
    auto caption=[&](unsigned i,const char *text,int x,int y,bool inverse=false) {
        ui::compact::setText(m_captions[i],text);setVisible(m_captions[i],true);
        // Reset the digital year's centered anchor when reusing a caption.
        lv_obj_align(m_captions[i],LV_ALIGN_TOP_LEFT,x,y);lv_obj_set_style_text_color(m_captions[i],inverse?t::onAccent():t::muted(),0);
    };
    lv_obj_set_style_bg_color(root(),t::bg(),0);
    lv_obj_set_style_bg_color(m_dial,t::tiles()?t::surface():t::bg(),0);
    // The warm date chip sits behind the dial. An inset outline supplies the
    // hybrid rim without a border shifting the ticks' and hands' origin.
    lv_obj_set_style_bg_opa(m_dial,t::warm()&&!hybrid?LV_OPA_TRANSP:LV_OPA_COVER,0);
    lv_obj_set_style_outline_width(m_dial,warmHybrid?1:0,0);
    lv_obj_set_style_outline_pad(m_dial,-1,0);
    lv_obj_set_style_outline_color(m_dial,t::edge(),0);
    lv_obj_set_style_line_color(m_secondHand,t::accent(),0);
    lv_obj_set_style_text_color(m_centerDate,t::muted(),0);
    lv_obj_set_style_text_color(m_centerTime,t::fg(),0);
    lv_obj_set_style_text_color(m_date,t::muted(),0);
    lv_obj_set_style_text_color(m_weekday,t::accent(),0);
    lv_obj_set_style_text_color(m_seconds,t::accent(),0);
    lv_obj_set_style_text_font(m_date,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_font(m_weekday,t::tiles()||t::focus()||t::warm()?&lv_font_montserrat_18:t::fontLarge(),0);
    lv_obj_set_style_text_font(m_seconds,t::fontLarge(),0);
    for(unsigned i=0;i<4;++i)lv_obj_set_style_text_color(m_digits[i],t::tiles()&&i<2?t::onAccent():(t::warm()||t::focus())&&i>=2?t::accent():t::terminal()?t::accent():t::fg(),0);
    if(digital) {
        if(t::warm()) {
            for(auto block:m_blocks){lv_obj_set_style_border_width(block,1,0);lv_obj_set_style_border_color(block,t::edge(),0);}
            caption(0,"Y U M O",95,15);
        } else if(t::focus()) {
            for(auto block:m_blocks)lv_obj_set_style_bg_color(block,t::edge(),0);
            caption(0,"24H",207,17);caption(1,"SEC",8,153);
        } else if(t::tiles()) {
            lv_obj_set_style_bg_color(m_blocks[0],t::accent(),0);
            lv_obj_set_style_radius(m_blocks[0],10,0);
            caption(0,"HOURS",10,10,true);caption(1,"SEC",0,54);caption(2,"MIN",12,181);
            lv_obj_set_style_text_font(m_captions[0],&lv_font_montserrat_12,0);
            lv_obj_align(m_captions[1],LV_ALIGN_TOP_MID,73,54);
            lv_obj_set_style_text_font(m_captions[2],&lv_font_montserrat_12,0);
            caption(3,"0000.00",10,212);
            lv_obj_set_style_text_font(m_captions[3],&lv_font_montserrat_12,0);
            lv_obj_set_width(m_captions[3],100);
            // Stable, centered cells in the two right-hand tiles.
            lv_obj_set_width(m_weekday,68);
            lv_obj_set_style_text_align(m_weekday,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_width(m_date,64);
            lv_obj_set_style_text_font(m_date,&lv_font_montserrat_24,0);
            lv_obj_set_style_text_color(m_date,t::fg(),0);
            lv_obj_set_style_text_align(m_date,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_width(m_seconds,64);
            lv_obj_set_style_text_font(m_seconds,&lv_font_montserrat_40,0);
            lv_obj_set_style_text_color(m_seconds,t::fg(),0);
            lv_obj_set_style_text_align(m_seconds,LV_TEXT_ALIGN_CENTER,0);
        } else if(t::terminal()) {
            lv_obj_set_style_bg_color(m_blocks[1],t::accent(),0);
            lv_obj_set_style_bg_color(m_blocks[2],t::edge(),0);
            caption(0,"YUMO",6,6);caption(1,"DATE",7,45);caption(2,"DAY",7,172);caption(3,"SEC",15,210,true);
        } else {
            // One square outer frame and two single-pixel separators, not
            // three rounded cards. Keep the live fields in their sample rows.
            for(auto block:m_blocks) {lv_obj_set_style_radius(block,0,0);lv_obj_set_style_bg_color(block,t::edge(),0);}
            lv_obj_set_style_border_width(m_blocks[0],1,0);
            lv_obj_set_style_border_color(m_blocks[0],t::edge(),0);
            lv_obj_set_style_bg_opa(m_blocks[0],LV_OPA_TRANSP,0);
            caption(0,"TIME / 24H",9,10);caption(1,"SEC",9,204);
            lv_obj_set_style_text_font(m_captions[0],&lv_font_montserrat_14,0);
            lv_obj_set_style_text_color(m_captions[0],t::fg(),0);
            lv_obj_set_style_text_font(m_captions[1],&lv_font_montserrat_14,0);
            lv_obj_set_width(m_date,140);
            lv_obj_set_width(m_weekday,68);
            lv_obj_set_style_text_font(m_weekday,&lv_font_montserrat_14,0);
            lv_obj_set_style_text_align(m_weekday,LV_TEXT_ALIGN_RIGHT,0);
            lv_obj_set_width(m_seconds,64);
            lv_obj_set_style_text_font(m_seconds,&lv_font_montserrat_32,0);
            lv_obj_set_style_text_align(m_seconds,LV_TEXT_ALIGN_RIGHT,0);
            lv_obj_set_style_text_color(m_seconds,t::fg(),0);
            lv_obj_set_width(m_colon,20);
            lv_obj_set_style_text_align(m_colon,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_color(m_colon,t::fg(),0);
        }
        setVisible(m_colon,!t::tiles()&&!t::warm()&&!t::focus());
        layoutDigitalFace();
        return;
    }
    int x=0,y=0,size=240;
    if(hybrid) {
        if(t::warm()) {
            x=3;y=43;size=152;
            box(0,159,53,1,136);lv_obj_set_style_bg_color(m_blocks[0],t::edge(),0);
            caption(0,"Y U M O",12,13);
            lv_obj_set_style_text_font(m_captions[0],&lv_font_montserrat_12,0);
            lv_obj_set_style_text_color(m_captions[0],t::accent(),0);
            lv_obj_align(m_centerDate,LV_ALIGN_TOP_RIGHT,-11,14);
            lv_obj_align(m_centerTime,LV_ALIGN_TOP_MID,79,73);
            setVisible(m_seconds,true);lv_obj_align(m_seconds,LV_ALIGN_TOP_MID,79,128);
            lv_obj_set_style_text_font(m_seconds,&lv_font_montserrat_40,0);
            setVisible(m_weekday,true);lv_obj_align(m_weekday,LV_ALIGN_TOP_MID,0,218);
            lv_obj_set_style_text_font(m_weekday,&lv_font_montserrat_14,0);
            lv_obj_set_style_text_color(m_weekday,t::muted(),0);
        } else if(t::focus()) {
            x=92;y=64;size=144;
            // Reuse the fixed 84px hour glyphs with top-left anchors.
            for(unsigned i=0;i<2;++i) {
                setVisible(m_digits[i],true);
                lv_obj_align(m_digits[i],LV_ALIGN_TOP_LEFT,8+i*48,4);
            }
            lv_obj_align(m_centerTime,LV_ALIGN_TOP_LEFT,8,146);
            lv_obj_set_style_text_color(m_centerTime,t::accent(),0);
            lv_obj_align(m_centerDate,LV_ALIGN_TOP_RIGHT,-10,39);
            setVisible(m_weekday,true);lv_obj_align(m_weekday,LV_ALIGN_TOP_RIGHT,-10,15);
            lv_obj_set_style_text_font(m_weekday,&lv_font_montserrat_14,0);
            setVisible(m_seconds,true);lv_obj_align(m_seconds,LV_ALIGN_TOP_LEFT,8,202);
            lv_obj_set_style_text_font(m_seconds,&lv_font_montserrat_12,0);
            lv_obj_set_style_text_color(m_seconds,t::muted(),0);
            caption(3,"0000",8,226);
        } else if(t::tiles()) {
            x=4;y=4;size=158;
            box(0,0,0,166,166);box(1,170,0,70,166,true);box(2,0,170,240,70);
            caption(0,"SEC",0,14,true);
            lv_obj_set_style_text_font(m_captions[0],&lv_font_montserrat_12,0);
            lv_obj_align(m_captions[0],LV_ALIGN_TOP_MID,85,14);
            setVisible(m_seconds,true);lv_obj_align(m_seconds,LV_ALIGN_TOP_MID,85,56);
            lv_obj_set_style_text_font(m_seconds,&lv_font_montserrat_40,0);
            lv_obj_set_style_text_color(m_seconds,t::onAccent(),0);
            setVisible(m_weekday,true);lv_obj_align(m_weekday,LV_ALIGN_TOP_MID,85,126);
            lv_obj_set_style_text_font(m_weekday,&lv_font_montserrat_12,0);
            lv_obj_set_style_text_color(m_weekday,t::onAccent(),0);
            lv_obj_align(m_centerTime,LV_ALIGN_TOP_LEFT,9,175);
            lv_obj_align(m_centerDate,LV_ALIGN_TOP_RIGHT,-9,178);
            lv_obj_set_style_text_color(m_centerDate,t::accent(),0);
            caption(3,"0000",0,209);
            lv_obj_set_style_text_font(m_captions[3],&lv_font_montserrat_12,0);
            lv_obj_align(m_captions[3],LV_ALIGN_TOP_RIGHT,-9,209);
        } else {
            x=41;y=77;size=158;
            if(t::terminal()) {
                box(0,3,24,234,49,true);caption(0,"YUMO",6,6);
                lv_obj_align(m_centerTime,LV_ALIGN_TOP_LEFT,12,29);
                lv_obj_set_style_text_color(m_centerTime,t::onAccent(),0);
                lv_obj_align(m_centerDate,LV_ALIGN_TOP_RIGHT,-6,6);
                lv_obj_set_style_text_font(m_centerDate,&lv_font_montserrat_10,0);
                setVisible(m_seconds,true);lv_obj_align(m_seconds,LV_ALIGN_TOP_RIGHT,-9,34);
                lv_obj_set_style_text_color(m_seconds,t::onAccent(),0);
            } else {
                box(0,0,73,240,1);lv_obj_set_style_bg_color(m_blocks[0],t::edge(),0);
                lv_obj_align(m_centerTime,LV_ALIGN_TOP_LEFT,8,8);
                lv_obj_align(m_centerDate,LV_ALIGN_TOP_LEFT,8,55);
                setVisible(m_seconds,true);lv_obj_align(m_seconds,LV_ALIGN_TOP_RIGHT,-8,12);
            }
        }
        lv_obj_set_style_text_font(m_centerTime,t::focus()?&lv_font_montserrat_48:t::warm()||t::tiles()||t::current()==t::Id::Mono?&lv_font_montserrat_40:&lv_font_montserrat_32,0);
    } else if(t::warm()) {
        x=2;y=2;size=236;box(0,82,160,76,27);
        lv_obj_set_style_radius(m_blocks[0],3,0);
        lv_obj_align(m_centerDate,LV_ALIGN_TOP_MID,0,167);
        lv_obj_set_style_text_color(m_centerDate,t::accent(),0);
        // Do not add a border to the dial: it would offset all child ticks
        // and hands relative to the geometric center on subsequent faces.
    } else if(t::focus()) {
        x=1;y=1;size=238;
        lv_obj_align(m_centerDate,LV_ALIGN_TOP_MID,0,160);
    } else if(t::tiles()) {
        x=21;y=5;size=198;box(0,2,207,236,31,true);
        lv_obj_align(m_centerDate,LV_ALIGN_BOTTOM_MID,0,-9);lv_obj_set_style_text_color(m_centerDate,t::onAccent(),0);
    } else if(t::terminal()) {
        // Keep one date/weekday row above the dial; reclaim the old footer.
        x=10;y=20;size=220;
        lv_obj_align(m_centerDate,LV_ALIGN_TOP_LEFT,6,5);
        lv_obj_set_style_text_font(m_centerDate,&lv_font_montserrat_10,0);
        setVisible(m_weekday,true);
        lv_obj_set_style_text_font(m_weekday,&lv_font_montserrat_10,0);
        lv_obj_align(m_weekday,LV_ALIGN_TOP_RIGHT,-6,5);
    } else {
        x=7;y=7;size=226;lv_obj_align(m_centerDate,LV_ALIGN_TOP_MID,0,165);
        box(0,0,0,240,240);lv_obj_set_style_bg_opa(m_blocks[0],LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(m_blocks[0],1,0);lv_obj_set_style_border_color(m_blocks[0],t::edge(),0);
    }
    m_dialPixels=size;lv_obj_set_size(m_dial,size,size);
    // set_pos only changes offsets in LVGL; it does NOT clear a prior CENTER.
    // Every face's x/y is an absolute top-left within the 240px face area.
    lv_obj_align(m_dial,LV_ALIGN_TOP_LEFT,x,y);
    lv_obj_set_style_text_font(m_centerDate,t::terminal()?&lv_font_montserrat_10:(t::warm()||t::focus()||(t::tiles()&&hybrid)||(!hybrid&&t::current()==t::Id::Mono))?&lv_font_montserrat_12:&lv_font_montserrat_14,0);
    const int center=size/2;
    for(int i=0;i<60;++i) {
        const double a=(i*6.0-90.0)*kPi/180.0;
        const int outer=center-2,inner=outer-(i%5==0?7:2);
        m_tickPoints[i][0]={static_cast<lv_value_precise_t>(center+std::cos(a)*inner),static_cast<lv_value_precise_t>(center+std::sin(a)*inner)};
        m_tickPoints[i][1]={static_cast<lv_value_precise_t>(center+std::cos(a)*outer),static_cast<lv_value_precise_t>(center+std::sin(a)*outer)};
        lv_line_set_points(m_tickLines[i],m_tickPoints[i],2);
    }
    const int r=center-(smallHybrid?16:26);
    const int nx[]={0,r,0,-r},ny[]={-r,0,r,0};
    for(unsigned i=0;i<4;++i) {
        setVisible(m_numerals[i],!hybrid||smallHybrid);
        lv_obj_set_style_text_font(m_numerals[i],smallHybrid?&lv_font_montserrat_12:t::terminal()?&lv_font_montserrat_14:t::fontLarge(),0);
        lv_obj_set_style_bg_opa(m_numerals[i],t::terminal()?LV_OPA_COVER:LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_color(m_numerals[i],t::accent(),0);
        lv_obj_set_style_text_color(m_numerals[i],t::terminal()?t::onAccent():t::fg(),0);
        lv_obj_align(m_numerals[i],LV_ALIGN_CENTER,nx[i],ny[i]);
    }
    ui::compact::setText(m_dialBrand,"YUMO");
    lv_obj_align(m_dialBrand,LV_ALIGN_CENTER,0,-size/5);setVisible(m_dialBrand,!hybrid);
}

void Watchface::refresh(void)
{
    time_t now = time(nullptr);
    if(now==m_lastRendered) return;
    m_lastRendered=now;
    struct tm value;
    localtime_r(&now, &value);

    if(m_face==3) {m_almanac.refresh(value,m_visible && !m_powerSaving);return;}

    const bool digital = m_face == 0;
    const bool hybrid = m_face == 2;

    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", value.tm_hour, value.tm_min);

    static const char *weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    char dateBuf[40];
    snprintf(dateBuf,sizeof(dateBuf),"%04d.%02d.%02d",value.tm_year+1900,value.tm_mon+1,value.tm_mday);

    // Only touch the elements that belong to the visible face. Updating hidden
    // labels/hands wastes CPU every tick and keeps the analog subtree (with its
    // many children) invalidated even when the digital face is showing.
    if (digital) {
        const int values[]={value.tm_hour/10,value.tm_hour%10,value.tm_min/10,value.tm_min%10};
        for(unsigned i=0;i<4;++i) {
            char digit[]={static_cast<char>('0'+values[i]),0};
            ui::compact::setText(m_digits[i],digit);
        }
        if(ui::theme::tiles()) {
            char day[8];snprintf(day,sizeof(day),"%02d",value.tm_mday);
            ui::compact::setText(m_date,day);
            char yearMonth[32];snprintf(yearMonth,sizeof(yearMonth),"%04d.%02d",value.tm_year+1900,value.tm_mon+1);
            ui::compact::setText(m_captions[3],yearMonth);
        } else {
            ui::compact::setText(m_date,dateBuf);
        }
        ui::compact::setText(m_weekday,weekdays[value.tm_wday % 7]);
        char seconds[16];snprintf(seconds,sizeof(seconds),"%02d",value.tm_sec);
        ui::compact::setText(m_seconds,seconds);
        return;
    }

    if (hybrid) {
        if(ui::theme::tiles() || ui::theme::focus()) {
            char date[16];snprintf(date,sizeof(date),"%02d.%02d",value.tm_mon+1,value.tm_mday);
            ui::compact::setText(m_centerDate,date);
            char year[12];snprintf(year,sizeof(year),"%04d",value.tm_year+1900);
            ui::compact::setText(m_captions[3],year);
        } else {
            ui::compact::setText(m_centerDate, dateBuf);
        }
        if(ui::theme::focus()) {
            const int hours[]={value.tm_hour/10,value.tm_hour%10};
            for(unsigned i=0;i<2;++i) {
                char digit[]={static_cast<char>('0'+hours[i]),0};
                ui::compact::setText(m_digits[i],digit);
            }
            char minute[8];snprintf(minute,sizeof(minute),"%02d",value.tm_min);
            ui::compact::setText(m_centerTime,minute);
            char seconds[16];snprintf(seconds,sizeof(seconds),"%02d SEC",value.tm_sec);
            ui::compact::setText(m_seconds,seconds);
        } else if(ui::theme::warm()) {
            char hour[8],minute[8];snprintf(hour,sizeof(hour),"%02d",value.tm_hour);snprintf(minute,sizeof(minute),"%02d",value.tm_min);
            ui::compact::setText(m_centerTime,hour);ui::compact::setText(m_seconds,minute);
        } else {
            ui::compact::setText(m_centerTime, timeBuf);
            char sec[8];snprintf(sec,sizeof(sec),"%02d",value.tm_sec);ui::compact::setText(m_seconds,sec);
        }
        // Format each reused label once, rather than changing it twice per tick.
        if(ui::theme::warm()) {
            char footer[24];snprintf(footer,sizeof(footer),"%s / %02d SEC",weekdays[value.tm_wday%7],value.tm_sec);
            ui::compact::setText(m_weekday,footer);
        } else {
            ui::compact::setText(m_weekday,weekdays[value.tm_wday%7]);
        }
    } else if(ui::theme::warm() || ui::theme::focus()) {
        char date[16];snprintf(date,sizeof(date),"%s %02d",weekdays[value.tm_wday%7],value.tm_mday);
        ui::compact::setText(m_centerDate,date);
    } else if(ui::theme::current()==ui::theme::Id::Mono) {
        char date[16];snprintf(date,sizeof(date),"%02d.%02d",value.tm_mon+1,value.tm_mday);
        ui::compact::setText(m_centerDate,date);
    } else if(ui::theme::terminal()) {
        ui::compact::setText(m_centerDate,dateBuf);
        ui::compact::setText(m_weekday,weekdays[value.tm_wday%7]);
    } else {
        char date[40];snprintf(date,sizeof(date),"%s %04d.%02d.%02d",weekdays[value.tm_wday%7],value.tm_year+1900,value.tm_mon+1,value.tm_mday);
        ui::compact::setText(m_centerDate,date);
    }

    const int center=m_dialPixels/2;
    const bool warmHybrid=hybrid&&ui::theme::warm();
    const bool tilesHybrid=hybrid&&ui::theme::tiles();
    const bool focusHybrid=hybrid&&ui::theme::focus();
    const bool previewHybrid=warmHybrid||tilesHybrid||focusHybrid;
    const int secondLength=center-(warmHybrid?17:tilesHybrid?16:focusHybrid?7:14);
    setHand(m_secondHand, m_secondPoints, value.tm_sec * 6.0, secondLength, previewHybrid?7:10,center);
    setHand(m_minuteHand, m_minutePoints,
            value.tm_min * 6.0 + value.tm_sec * 0.1, previewHybrid?secondLength*82/100:center*7/10, previewHybrid?6:8,center);
    setHand(m_hourHand, m_hourPoints,
            (value.tm_hour % 12) * 30.0 + value.tm_min * 0.5, previewHybrid?secondLength*57/100:center/2, previewHybrid?4:6,center);
}

}// namespace apps
