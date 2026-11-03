/*
 * Window / navigation manager implementation.
 */
#include "window_mgr.hpp"
#include "home_screen.hpp"
#include "gesture.hpp"
#include "theme.hpp"
#include "compact_ui.hpp"
#include "text_input.hpp"
#include "external_keyboard.hpp"
#include "quick_panel.hpp"
#include "screen_protection.hpp"
#include "../core/event_bus.hpp"
#include "../core/button_mgr.hpp"
#include "../core/battery_mgr.hpp"
#include "../core/connectivity.hpp"
#include "../core/system_service.hpp"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <new>
#include "esp_log.h"
#include "../core/power_mgr.hpp"

namespace ui {

namespace {
constexpr int kStatusBarHeight = 24;
constexpr int kScreenHeight = 240;
constexpr int kContentHeight = kScreenHeight - kStatusBarHeight;
lv_obj_t *s_statusTime = nullptr;
lv_obj_t *s_statusWifi = nullptr;
lv_obj_t *s_statusBluetooth = nullptr;
lv_obj_t *s_statusBattery = nullptr;

void statusColor(lv_obj_t *label,lv_color_t color)
{
    lv_style_value_t previous;
    if(lv_obj_get_local_style_prop(label,LV_STYLE_TEXT_COLOR,&previous,0)!=LV_STYLE_RES_FOUND ||
        !lv_color_eq(previous.color,color))lv_obj_set_style_text_color(label,color,0);
}

void updateStatusBar(lv_timer_t *)
{
    if (!sys::Power::isScreenOn() || !s_statusTime ||
        lv_obj_is_hidden(lv_obj_get_parent(s_statusTime))) return;
    time_t now = time(nullptr);
    struct tm current;
    localtime_r(&now, &current);
    if (s_statusTime) {
        char text[12];std::snprintf(text,sizeof(text),"%02d:%02d",current.tm_hour,current.tm_min);
        compact::setText(s_statusTime,text);
    }
    const auto wifi=sys::Connectivity::snapshot();
    const auto bt=sys::SystemService::bluetooth();
    // Same source as settings: hidden=off, grey=on/unconnected,
    // amber=transition, green=connected, red=error.
    auto radioIcon=[](lv_obj_t *label,const char *icon,bool enabled,bool connected,bool pending,bool failed) {
        if(!label) return;
        compact::setText(label,(enabled || failed)?icon:"");
        statusColor(label,failed?theme::danger():connected?ui::theme::success():pending?ui::theme::warning():theme::muted());
    };
    radioIcon(s_statusWifi,LV_SYMBOL_WIFI,wifi.enabled,wifi.state==sys::WifiState::Connected,
        wifi.transitioning || wifi.state==sys::WifiState::Searching || wifi.state==sys::WifiState::Connecting,wifi.state==sys::WifiState::Failed);
    radioIcon(s_statusBluetooth,LV_SYMBOL_BLUETOOTH,bt.enabled,bt.state==sys::BleHidState::Connected,
        bt.state==sys::BleHidState::Starting || bt.state==sys::BleHidState::Connecting,bt.hasError());
    if (s_statusBattery) {
        const auto battery=sys::Battery::snapshot();
        const int level=battery.percent;
        const char *icon=LV_SYMBOL_BATTERY_EMPTY;
        if(battery.valid) icon=level>=95?LV_SYMBOL_BATTERY_FULL:level>=65?LV_SYMBOL_BATTERY_3:
            level>=35?LV_SYMBOL_BATTERY_2:level>=10?LV_SYMBOL_BATTERY_1:LV_SYMBOL_BATTERY_EMPTY;
        if(battery.chargingKnown && battery.charging) icon=LV_SYMBOL_CHARGE;
        char text[24];
        if(battery.valid) std::snprintf(text,sizeof(text),"%s %d%%",icon,level);
        else std::snprintf(text,sizeof(text),"%s --%%",icon);
        if(std::strcmp(lv_label_get_text(s_statusBattery),text)!=0) lv_label_set_text(s_statusBattery,text);
        statusColor(s_statusBattery,battery.charging?ui::theme::success():
            battery.valid && level<=10?theme::danger():theme::fg());
    }
}
}

WindowManager &WindowManager::instance(void)
{
    static WindowManager s_inst;
    return s_inst;
}

void WindowManager::init(void)
{
    if (m_screen) return;
    // Use the active display screen as our root canvas.
    m_screen = lv_screen_active();
    lv_obj_set_style_bg_color(m_screen, theme::bg(), 0);
    lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(m_screen, 0, 0);
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_set_style_radius(m_screen, 0, 0);
    lv_obj_set_style_text_color(m_screen, theme::fg(), 0);
    lv_obj_set_scrollable(m_screen,false);

    // Build the persistent home layer.
    m_home = lv_obj_create(m_screen);
    lv_obj_remove_style_all(m_home);
    lv_obj_set_size(m_home, LV_PCT(100), kContentHeight);
    lv_obj_set_pos(m_home, 0, kStatusBarHeight);
    lv_obj_set_scrollable(m_home,false);
    HomeScreen::build(m_home);

    // Keep a compact status bar above both the home screen and app windows.
    m_statusBar = lv_obj_create(m_screen);
    lv_obj_set_size(m_statusBar, LV_PCT(100), kStatusBarHeight);
    lv_obj_align(m_statusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(m_statusBar, theme::bg(), 0);
    lv_obj_set_style_text_color(m_statusBar, theme::fg(), 0);
    lv_obj_set_style_bg_opa(m_statusBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_statusBar, 0, 0);
    // Keep the clock / battery labels off the panel border.
    lv_obj_set_style_pad_hor(m_statusBar, theme::kStatusPad, 0);
    lv_obj_set_style_pad_ver(m_statusBar, 0, 0);
    lv_obj_set_scrollable(m_statusBar,false);

    s_statusTime = lv_label_create(m_statusBar);
    lv_obj_align(s_statusTime, LV_ALIGN_LEFT_MID, 0, 0);
    s_statusWifi = lv_label_create(m_statusBar);
    lv_obj_align(s_statusWifi, LV_ALIGN_CENTER, -12, 0);
    s_statusBluetooth = lv_label_create(m_statusBar);
    lv_obj_align(s_statusBluetooth, LV_ALIGN_CENTER, 12, 0);
    s_statusBattery = lv_label_create(m_statusBar);
    lv_obj_align(s_statusBattery, LV_ALIGN_RIGHT_MID, 0, 0);
    for(auto label:{s_statusTime,s_statusWifi,s_statusBluetooth,s_statusBattery})
        lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);
    lv_timer_create(updateStatusBar, 1000, nullptr);
    updateStatusBar(nullptr);

    // React to navigation events from buttons / gestures.
    sys::EventBus::subscribe(sys::EventType::BackRequested,
                             [](const sys::Event &) { WindowManager::instance().goBack(); });
    sys::EventBus::subscribe(sys::EventType::HomeRequested,
                             [](const sys::Event &) {
        // Also reject a BOOT double-click queued just before entering a game.
        if(!QuickPanel::back() && !sys::Buttons::blockHomeShortcut()) WindowManager::instance().goHome();
    });
}

void WindowManager::registerApp(const char *name, const char *icon, AppFactory factory,const char *key)
{
    m_apps.push_back({name, icon, std::move(factory),sys::app_order::key(key?key:name)});
}

void WindowManager::createWindow(std::unique_ptr<App> app)
{
    sys::Power::activity();
    if (!app || !m_screen) { showTop();return; }
    // A separate screen keeps the home object tree out of app compositing.
    // The screen owns the opaque backdrop even if an app restyles its root.
    m_appScreen = lv_obj_create(nullptr);
    if(!m_appScreen) { showTop();return; }
    lv_obj_remove_style_all(m_appScreen);
    lv_obj_set_style_bg_color(m_appScreen, theme::bg(), 0);
    lv_obj_set_style_bg_opa(m_appScreen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(m_appScreen, false);
    lv_obj_t *win = lv_obj_create(m_appScreen);
    if(!win) {
        lv_obj_delete(m_appScreen);m_appScreen=nullptr;
        showTop();return;
    }
    lv_obj_remove_style_all(win);
    lv_obj_set_size(win, LV_PCT(100), kScreenHeight);
    lv_obj_set_pos(win, 0, 0);
    lv_obj_set_style_bg_color(win, theme::bg(), 0);
    lv_obj_set_style_text_color(win, theme::fg(), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(win,false);
    m_active=std::move(app);
    m_active->setRoot(win);
    m_active->onCreate(win);
    if (!m_active->handlesHorizontalGesture()) Gesture::hook(win);
    lv_obj_set_hidden(m_home,true);
    lv_obj_set_hidden(m_statusBar,true);
    lv_screen_load(m_appScreen);
    m_active->onShow();
    // Replace the entire home frame, including its bottom navigation dock.
    // Only do this on navigation; ordinary app updates remain partial redraws.
    lv_obj_update_layout(m_appScreen);
    lv_obj_invalidate(m_appScreen);
}

void WindowManager::openApp(size_t index)
{
    if(index<m_apps.size() && !sys::Power::sleepPending()) m_pendingApp=index;
}

void WindowManager::processNavigation()
{
    HomeScreen::poll();
    const bool shutdownError=sys::Power::takeShutdownError();
    if(sys::Power::takeThemeRestartError() || shutdownError) {
        // System notices outlive scene navigation until their expiry timer.
        auto toast=compact::panel(lv_layer_top(),240,74);
        lv_obj_set_style_bg_color(toast,theme::surface(),0);
        lv_obj_align(toast,LV_ALIGN_BOTTOM_MID,0,0);
        auto text=compact::label(toast,shutdownError?"关机未完成\n请稍后到设置中重试":"主题切换未完成\n请稍后到设置中重试",230);
        lv_label_set_long_mode(text,LV_LABEL_LONG_MODE_WRAP);lv_obj_center(text);
        auto timer=lv_timer_create([](lv_timer_t *t) {
            lv_obj_delete(static_cast<lv_obj_t *>(lv_timer_get_user_data(t)));
        },4000,toast);
        lv_timer_set_repeat_count(timer,1);
    }
    if(m_pendingApp>=m_apps.size() || sys::Power::sleepPending()) return;
    const auto index=m_pendingApp;
    m_pendingApp=static_cast<size_t>(-1);
    // Dispose old timers, subscriptions, storage jobs, images and widgets
    // BEFORE invoking the next factory. No hidden/preloaded app remains.
    closeActive();
    try {
        createWindow(m_apps[index].factory());
    } catch(const std::bad_alloc &) {
        ESP_LOGW("window","App allocation failed; returning home");
        closeActive();showTop();
    }
}

void WindowManager::closeActive()
{
    ScreenProtection::reset();
    HomeScreen::cancelArrange();
    QuickPanel::close();
    TextInput::hide();
    ExternalKeyboard::resetFocus();
    if(!m_active) return;
    // A failing save must not keep the old app's widgets or subscriptions alive.
    try { m_active->onHide(); } catch(const std::bad_alloc &) { ESP_LOGW("window","App hide allocation failed"); }
    try { m_active->onDestroy(); } catch(const std::bad_alloc &) { ESP_LOGW("window","App destroy allocation failed"); }
    // LVGL must have a live active screen while the outgoing scene is deleted.
    // Navigation is processed in one GUI turn, so no intermediate home frame
    // is flushed during an app-to-app switch.
    lv_screen_load(m_screen);
    if(m_active->root()) lv_obj_delete(m_active->root());
    m_active->setRoot(nullptr);
    m_active.reset();
    if(m_appScreen) { lv_obj_delete(m_appScreen);m_appScreen=nullptr; }
}

void WindowManager::goBack(void)
{
    if(QuickPanel::back())return;
    if(!m_active&&HomeScreen::cancelArrange())return;
    m_pendingApp=static_cast<size_t>(-1);
    if(TextInput::visible()) { TextInput::hide();return; }
    if(!m_active || m_active->onBack()) return;
    closeActive();showTop();
}

void WindowManager::goHome(void)
{
    m_pendingApp=static_cast<size_t>(-1);
    closeActive();showTop();
}

lv_obj_t *WindowManager::inputRoot() const {
    if(auto panel=QuickPanel::root())return panel;
    return !m_active?m_home:m_active->root();
}
bool WindowManager::allowsQuickPanel() const {
    return !HomeScreen::arranging() && m_pendingApp>=m_apps.size() && (!m_active || m_active->allowsQuickPanel());
}
bool WindowManager::allowsScreenProtection() const {
    return !HomeScreen::arranging() && m_pendingApp>=m_apps.size() &&
        (!m_active || m_active->allowsScreenProtection());
}
bool WindowManager::directionalKey(lv_dir_t direction) {
    if(QuickPanel::visible() || TextInput::visible() || m_pendingApp<m_apps.size()) return false;
    return m_active && m_active->onDirectionalKey(direction);
}
uint32_t WindowManager::powerHints() const {
    const auto hints=m_active?m_active->powerHints():sys::power::None;
    return QuickPanel::visible()?hints&sys::power::KeepSession:hints;
}

void WindowManager::showTop(void)
{
    if(!m_active) {
        if(lv_screen_active()!=m_screen) lv_screen_load(m_screen);
        lv_obj_set_hidden(m_home,false);
        lv_obj_set_hidden(m_statusBar,false);
        lv_obj_move_foreground(m_statusBar);
        lv_obj_invalidate(m_screen);
    }
}

}// namespace ui
