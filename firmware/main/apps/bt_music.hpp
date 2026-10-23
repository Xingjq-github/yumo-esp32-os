#pragma once
#include "app.hpp"
#include "lvgl.h"
namespace apps {
class BtMusic : public ui::App {
public:
    const char *name() const override { return "蓝牙音乐"; }
    const char *icon() const override { return LV_SYMBOL_BLUETOOTH; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
private:
    static void actionCb(lv_event_t *e),statusTimerCb(lv_timer_t *timer);
    void refreshStatus();
    lv_obj_t *m_link=nullptr,*m_buttons[6]{};
    lv_timer_t *m_statusTimer=nullptr;
    bool m_visible=false; int m_railState=-1; uint32_t m_errorUntil=0;
};
} // namespace apps
