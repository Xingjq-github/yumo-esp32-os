#pragma once
#include "app.hpp"
#include "hid_keyboard_model.hpp"
#include "lvgl.h"
#include <cstdint>
namespace apps {
class BtKeyboardMouse : public ui::App {
public:
    const char *name() const override { return "蓝牙键鼠"; }
    const char *icon() const override { return LV_SYMBOL_KEYBOARD; }
    bool handlesHorizontalGesture() const override { return true; }
    bool allowsQuickPanel() const override {return false;}
    uint32_t powerHints() const override {return m_autoClick?sys::power::StayLit|sys::power::KeepSession:sys::power::None;}
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    static void switchCb(lv_event_t *e),tabCb(lv_event_t *e),shortcutCb(lv_event_t *e),keyCb(lv_event_t *e);
    static void clickRightCb(lv_event_t *e),speedCb(lv_event_t *e),autoClickCb(lv_event_t *e),rateCb(lv_event_t *e);
    static void touchpadCb(lv_event_t *e),wheelCb(lv_event_t *e);
    static void statusTimerCb(lv_timer_t *t),autoClickTimerCb(lv_timer_t *t);
    void refreshStatus(),updateSpeedLabel(),updateAutoLabel(),restartAutoClick();
    void showKeyboard(bool show),selectTab(unsigned tab),updateKeyLabels(),refreshPreview();
    bool commitPending(); void cancelPending(),releasePointer();
    bool emitMove(int32_t dx,int32_t dy,uint8_t buttons);
    lv_obj_t *m_status=nullptr,*m_switch=nullptr,*m_mouseBar=nullptr,*m_tabsBar=nullptr;
    lv_obj_t *m_pad=nullptr,*m_wheel=nullptr,*m_keys=nullptr,*m_shortcuts=nullptr,*m_charPanel=nullptr,*m_preview=nullptr;
    lv_obj_t *m_shortcutHints[12]{},*m_tabs[3]{},*m_digitLabels[10]{},*m_modeLabel=nullptr;
    lv_obj_t *m_speedLabel=nullptr,*m_autoLabel=nullptr;
    lv_timer_t *m_statusTimer=nullptr,*m_autoTimer=nullptr;
    bool m_visible=false,m_keyboard=false,m_speedX2=false,m_autoClick=false;
    unsigned m_tab=0; keys::Alphabet m_alphabet=keys::Alphabet::Lower; keys::MultiTap m_compose;
    uint32_t m_screenOffSub=0,m_errorUntil=0,m_nextStatusMs=0; bool m_wasConnected=false; int m_railState=-1;
    uintptr_t m_pointerLink=0,m_clickLink=0;
    int32_t m_autoRate=1;
    lv_point_t m_lastPoint{};int32_t m_totalMove=0;bool m_touching=false;
    float m_accX=0,m_accY=0;bool m_dragArmed=false,m_dragging=false;
    uint32_t m_lastReleaseMs=0,m_pressMs=0;int32_t m_wheelLastY=0;bool m_wheeling=false;
};
} // namespace apps
