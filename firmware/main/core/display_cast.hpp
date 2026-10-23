#pragma once
#include "esp_err.h"
#include "lvgl.h"
namespace sys {
class DisplayCast {
public:
    static void init(lv_display_t *display); // GUI only; no task/buffers at boot
    static esp_err_t setEnabled(bool on);    // GUI only; explicit session, no NVS
    static void poll();                     // GUI pump; never waits for USB
    static bool enabled();                 // GUI only
    static const char *statusText();        // GUI only
    static bool idle();                    // worker-safe deep-sleep fence
    static void cancel();                  // power task: latch cancellation without waiting
};
}
