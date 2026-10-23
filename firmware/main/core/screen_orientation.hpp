#pragma once
#include "lvgl.h"
#include "qmi8658.hpp"

namespace sys {
class ScreenOrientation {
public:
    static void init(lv_display_t *display,lv_indev_t *touch);
    static void poll(bool appLocked); // GUI task, after foreground navigation.
    static void mapAcceleration(Acceleration &value); // For locked gravity apps.
};
}
