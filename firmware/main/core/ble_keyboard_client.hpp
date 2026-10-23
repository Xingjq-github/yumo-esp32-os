#pragma once
#include <cstddef>
#include <cstdint>
namespace sys {
class BleKeyboardClient {
public:
    enum class Stage:uint8_t {Idle,Service,Characteristics,ReportMap,References,Subscribe,Ready,Failed};
    struct Diagnostics {Stage stage=Stage::Idle;uint32_t received=0,rejected=0;int error=0;uint32_t changes=0;};
    // All methods below run in NimBLE context; no blocking wait or extra task.
    static void start(uint16_t handle);
    static void reset();
    static void notification(uint16_t handle,uint16_t attribute,const uint8_t *data,size_t length);
    static bool ready();
    static bool keyboardReady();
    static bool gamepadReady();
    static bool iineKeyboard(); // Profile retained through GAP display-name refresh.
    static bool iineGamepad(); // Captured C-mode descriptor plus report topology.
    static Diagnostics diagnostics(); // Value-only snapshot; no key contents.
    static void diagnosticView(bool visible); // FC input-test page only; bounded serial capture.
};
}
