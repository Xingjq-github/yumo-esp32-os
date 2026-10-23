#pragma once
#include "lvgl.h"
#include "core/event_bus.hpp"
namespace ui {
// GUI-only system overlay. Widgets exist only while open; no App is preloaded.
class QuickPanel {
public:
    static void init(lv_indev_t *pointer);
    static void poll();
    static bool visible();
    static lv_obj_t *root();
    static bool back();
    static void close();
    static bool allowEvent(const sys::Event &event);
};
}
