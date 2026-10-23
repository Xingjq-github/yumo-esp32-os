#pragma once
#include "lvgl.h"
namespace sys {
class Screenshot {
public:
    enum class Notice {None,Started,Saved,Card,Space,Memory,Failed};
    static void init(lv_display_t *display); // GUI; no task/buffer until requested.
    static void request(); // Button task: atomic mailbox only.
    static void setViewBlocked(bool blocked); // GUI: full-screen photo owns KEY.
    static Notice poll(); // GUI: frame capture, result collection; never SD I/O.
    static bool idle(); // Sleep/restart fence includes the pending capture.
};
}
