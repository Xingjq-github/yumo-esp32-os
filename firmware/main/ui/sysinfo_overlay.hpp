/*
 * SysInfoOverlay — top-left floating readout showing runtime stats.
 *
 * Attached to lv_layer_top() so it stays visible across screens/apps.
 * Transparent background, tiny multi-color labels updated on a 1s timer.
 */
#pragma once

#include "lvgl.h"

namespace ui {

class SysInfoOverlay {
public:
    // Create the overlay lazily on lv_layer_top(). Safe to call multiple times.
    static void init(lv_display_t *disp);
    // Show/hide the overlay without destroying it.
    static void setVisible(bool on);
    static bool visible(void);
};

}// namespace ui