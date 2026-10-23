#pragma once
#include "core/hid_input_report.hpp"
namespace ui {
class ExternalKeyboard {
public:
    static void init(); // GUI context
    static void poll();
    static void resetFocus(); // before destroying an app tree
    static void rescope(); // system overlay; preserve the foreground App's raw capture
    using RawHandler=void (*)(const sys::hidinput::Keys &,void *);
    static void capture(RawHandler handler,void *context); // GUI-only; null releases
};
}
