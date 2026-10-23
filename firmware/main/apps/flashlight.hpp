#pragma once
#include "app.hpp"
#include <cstdint>
namespace apps {
class Flashlight : public ui::App {
public:
    const char *name() const override { return "手电筒"; }
    const char *icon() const override { return LV_SYMBOL_CHARGE; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool allowsQuickPanel() const override {return false;}
    uint32_t powerHints() const override {return m_on||m_sos?sys::power::StayLit|sys::power::LowLoad:sys::power::None;}
private:
    static void touchCb(lv_event_t *e);
    static void sosCb(lv_event_t *e);
    static void timerCb(lv_timer_t *timer);
    void setSos(bool enabled);
    void paint();
    void stop();
    lv_obj_t *m_sosButton=nullptr,*m_sosLabel=nullptr,*m_levelLabel=nullptr,*m_hint=nullptr;
    lv_timer_t *m_timer=nullptr;
    uint32_t m_screenOffSub=0,m_signalStarted=0;
    lv_point_t m_press{};
    int m_level=2,m_pressLevel=2,m_drawnLevel=-1;
    bool m_on=true,m_sos=false,m_visible=false,m_moved=false,m_lit=false,m_painted=false,m_restoreInfo=false;
};
} // namespace apps
