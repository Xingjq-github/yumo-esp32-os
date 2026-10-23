#pragma once
#include "app.hpp"
#include "bsp/touch_observer.h"
#include <cstdint>
namespace apps {
class TouchScope : public ui::App {
public:
    const char *name() const override { return "触控展示"; }
    const char *icon() const override { return LV_SYMBOL_EDIT; }
    bool handlesHorizontalGesture() const override { return true; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
private:
    static constexpr unsigned TrailCount=96;
    static constexpr uint32_t LifetimeMs=2000;
    struct Dot { lv_obj_t *obj=nullptr; uint32_t at=0; bool active=false; };
    static void timerCb(lv_timer_t *t);
    void update(),clear();
    void present(const bsp_touch_snapshot_t &sample,uint32_t now);
    Dot m_dots[TrailCount]; unsigned m_next=0;
    lv_obj_t *m_values[5]{}; lv_timer_t *m_timer=nullptr;
    uint32_t m_lastSequence=0,m_lastActivity=0,m_screenOffSub=0,m_screenOnSub=0;
    bool m_visible=false,m_down=false,m_textVisible=false;
    esp_err_t m_lastError=ESP_OK;
};
} // namespace apps
