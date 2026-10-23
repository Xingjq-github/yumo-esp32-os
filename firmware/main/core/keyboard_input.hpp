#pragma once
#include "hid_input_report.hpp"
namespace sys {
// Callbacks publish only a value snapshot. No LVGL calls, allocation or NVS.
class KeyboardInput {
public:
    static bool init(); // worker before starting the host; allocates once
    static void publish(const hidinput::Keys &keys);
    static void reset();
    static bool pop(hidinput::Keys &keys);
    static hidinput::Keys snapshot(uint32_t &revision);
};
}
