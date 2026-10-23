/* Clock app with several swipe-selectable watch faces. */
#pragma once

#include "app.hpp"
#include "lvgl.h"
#include "ui/almanac_clock.hpp"
#include <time.h>

namespace apps {

class Watchface : public ui::App {
public:
    const char *name(void) const override { return "Clock"; }
    const char *icon(void) const override;
    bool handlesHorizontalGesture(void) const override { return true; }
    bool onDirectionalKey(lv_dir_t direction) override;

    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy(void) override;
    uint32_t powerHints() const override {return sys::power::StayLit|sys::power::LowLoad|sys::power::Dim;}
    void onPowerSaving(bool enabled) override;

private:
    static void tickCb(lv_timer_t *t);
    static void touchCb(lv_event_t *e);
    static void pressCb(lv_event_t *e);
    void refresh(void);
    void applyFace(void);
    void changeFace(int delta);
    void layoutDigitalFace();
    void layoutFace();

    lv_obj_t *m_faceArea=nullptr;
    ui::AlmanacClock m_almanac;
    lv_obj_t *m_weekday=nullptr,*m_seconds=nullptr;
    lv_obj_t *m_digits[4]{},*m_colon=nullptr,*m_numerals[4]{},*m_dialBrand=nullptr;
    lv_point_precise_t m_tickPoints[60][2]{};
    lv_obj_t *m_blocks[4]{},*m_captions[4]{},*m_tickLines[60]{};
    int m_dialPixels=240;
    bool m_visible=false; time_t m_lastRendered=static_cast<time_t>(-1);
    bool m_powerSaving=false;
    lv_obj_t *m_date       = nullptr;
    lv_obj_t *m_dial       = nullptr;
    lv_obj_t *m_hourHand   = nullptr;
    lv_obj_t *m_minuteHand = nullptr;
    lv_obj_t *m_secondHand= nullptr;
    lv_obj_t *m_centerDate = nullptr;
    lv_obj_t *m_centerTime = nullptr;

    lv_timer_t *m_timer = nullptr;
    int m_face = 0;
    lv_coord_t m_pressStartX = 0;
    lv_coord_t m_pressStartY = 0;
    bool m_tracking = false;
    lv_point_precise_t m_hourPoints[2]{};
    lv_point_precise_t m_minutePoints[2]{};
    lv_point_precise_t m_secondPoints[2]{};
};

}// namespace apps
