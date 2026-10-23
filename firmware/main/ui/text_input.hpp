#pragma once
#include "lvgl.h"
namespace ui {
// GUI-thread only. One input bar for every app, no app pointers retained.
class TextInput {
public:
    static void init(lv_indev_t *pointer);
    static void bindField(lv_obj_t *field);
    static void show(lv_obj_t *field);
    static void hide();
    static bool visible();
    static lv_obj_t *root(); // Controller focus stays inside the visible input bar.
};
} // namespace ui
