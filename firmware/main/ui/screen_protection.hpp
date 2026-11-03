#pragma once
#include "lvgl.h"

namespace ui {
// GUI-only optical image-retention mitigation; never changes sleep or App state.
class ScreenProtection {
public:
    // Wrap the board read BEFORE QuickPanel wraps it, so the wake stroke cannot
    // start either a pull-down gesture or a pattern on the underlying page.
    static void init(lv_indev_t *pointer);
    static void poll(bool allowed);
    static void reset(); // navigation/sleep fence, keeps any wake stroke consumed
    static bool visible();
};
}
