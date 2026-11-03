/*
 * Window / navigation manager.
 *
 * Owns one foreground App on its own screen, alongside a persistent HomeScreen.
 * Handles navigation driven either by gestures/buttons (via EventBus
 * Back/HomeRequested) or by direct openApp() calls from the home grid.
 *
 * All methods must be called from the LVGL/GUI context.
 */
#pragma once

#include "app.hpp"
#include "core/app_order_model.hpp"
#include "lvgl.h"
#include <functional>
#include <memory>
#include <vector>

namespace ui {

// Factory that builds a fresh App instance on demand.
using AppFactory = std::function<std::unique_ptr<App>()>;

struct AppEntry {
    const char *name;
    const char *icon;
    AppFactory  factory;
    uint32_t id;
};

class WindowManager {
public:
    static WindowManager &instance(void);

    // Initialize: create the base screen and subscribe to navigation events.
    void init(void);

    // Register an app so it appears on the home grid and can be opened.
    void registerApp(const char *name, const char *icon, AppFactory factory,const char *key=nullptr);
    const std::vector<AppEntry> &apps(void) const { return m_apps; }

    // Open an app by index into the registered list.
    void openApp(size_t index); // defer creation until the current LVGL event returns
    void processNavigation(); // GUI pump only; never preloads registered factories

    // Navigation.
    void goBack(void); // App-internal back, otherwise destroy the foreground App
    void goHome(void); // destroy the foreground App and return home
    bool atHome(void) const { return !m_active; }
    const char *activeName() const { return m_active?m_active->name():"桌面"; } // GUI context only
    lv_obj_t *inputRoot() const;
    bool directionalKey(lv_dir_t direction);
    bool allowsQuickPanel() const;
    bool allowsScreenProtection() const;
    bool locksScreenOrientation() const {return m_active && m_active->locksScreenOrientation();}
    uint32_t powerHints() const;
    void powerSaving(bool enabled) {if(m_active)m_active->onPowerSaving(enabled);}

    // Current scene; the persistent home screen is detached while an App is open.
    lv_obj_t *screen(void) const { return m_appScreen ? m_appScreen : m_screen; }

private:
    WindowManager(void) = default;

    void showTop(void);
    void closeActive();
    void createWindow(std::unique_ptr<App> app);

    lv_obj_t                         *m_screen    = nullptr; // persistent home screen
    lv_obj_t                         *m_appScreen = nullptr; // owned foreground scene
    lv_obj_t    *m_home      = nullptr; // home container
    lv_obj_t                         *m_statusBar = nullptr; // persistent top bar
    std::vector<AppEntry>            m_apps;
    std::unique_ptr<App> m_active; // no covered/background app instances
    size_t m_pendingApp=static_cast<size_t>(-1);
};

}// namespace ui
