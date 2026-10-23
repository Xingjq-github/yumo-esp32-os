/*
 * Global gesture layer.
 *
 * Attaches swipe handling to a full-screen object and turns swipes into
 * navigation intents published on the EventBus:
 *
 *   swipe right -> BackRequested
 *   swipe up    -> HomeRequested
 *
 * Home-specific gestures (swipe up to reveal the app grid, pull down for the
 * quick panel) are handled by the HomeScreen itself.
 *
 * Call hook() on every full-screen window so the gesture works everywhere.
 */
#pragma once

#include "lvgl.h"

namespace ui {

class Gesture {
public:
    // Attach swipe -> navigation handling to a full-screen object.
    static void hook(lv_obj_t *obj);
};

}// namespace ui