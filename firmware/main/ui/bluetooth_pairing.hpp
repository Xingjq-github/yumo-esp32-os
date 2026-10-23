#pragma once
#include "lvgl.h"
namespace ui {
class BluetoothPairing {
public:
    static void poll(); // GUI thread, available from any app
    static lv_obj_t *root();
    static bool back(); // Same generation-checked cancellation as the button.
};
}
