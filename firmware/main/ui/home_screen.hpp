/*
 * Yumo（雨墨）home: a persistent, vertically scrollable two-column app menu.
 * Tiles open on-demand apps through WindowManager; no preloaded app instances.
 * The app grid is the home itself, not an overlay behind a home clock.
 */
#pragma once

#include "lvgl.h"

namespace ui {

class HomeScreen {
public:
    // Build the whole home UI into the given persistent parent container.
    static void build(lv_obj_t *parent);
    static void poll();
    static bool arranging();
    static bool cancelArrange();
};

}// namespace ui
