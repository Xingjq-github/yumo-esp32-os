/*
 * Base class for all applications in the new firmware.
 *
 * The WindowManager owns only one on-demand App instance. Each app builds its UI into
 * a full-screen root object supplied by onCreate(). Lifecycle:
 *
 *   onCreate(parent) -> build UI once, store root
 *   onShow()        -> becoming visible (also right after create)
 *   onHide()         -> leaving the foreground; stop app activity
 *   onDestroy()      -> exiting/switching app (free resources)
 *
 * Apps must not call LVGL from other threads; all lifecycle callbacks run in
 * the GUI/LVGL context.
 */
#pragma once

#include "lvgl.h"
#include "core/power_policy.hpp"

namespace ui {

class App {
public:
    virtual ~App(void) = default;

    // Display name shown on the home grid.
    virtual const char *name(void) const = 0;

    // Optional LVGL symbol / icon text for the home grid (may be nullptr).
    virtual const char *icon(void) const { return nullptr; }

    // Apps that consume horizontal swipes can suppress the global back gesture.
    virtual bool handlesHorizontalGesture(void) const {return false; }
    // Full-screen touch controls can reserve the top-edge gesture by scene.
    virtual bool allowsQuickPanel() const {return true;}
    // Keep the entry orientation throughout apps with gravity-based controls.
    virtual bool locksScreenOrientation() const {return false;}
    virtual uint32_t powerHints() const {return sys::power::None;}
    virtual void onPowerSaving(bool) {}
    // Explicit opt-in: media, reading and always-lit tools keep their own UI.
    virtual bool allowsScreenProtection() const {return false;}

    // Build the UI into the given full-screen parent. Called once.
    virtual void onCreate(lv_obj_t *parent) = 0;

    // Visibility transitions. Default no-ops.
    virtual void onShow(void) {}
    virtual void onHide(void) {}

    // Return true when an in-app page/dialog consumed Back. Home always exits.
    virtual bool onBack(void) { return false; }
    // Return true when the foreground page consumes a controller direction.
    virtual bool onDirectionalKey(lv_dir_t) { return false; }

    // Free any owned resources. The root object is deleted by the manager.
    virtual void onDestroy(void) {}

    // Root object accessor (set by the manager before onCreate).
    lv_obj_t *root(void) const { return m_root; }
    void      setRoot(lv_obj_t *root) { m_root = root; }

protected:
    lv_obj_t *m_root = nullptr;
};

}// namespace ui
