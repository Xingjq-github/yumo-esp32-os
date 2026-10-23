#pragma once
#include <cstdint>
#include <functional>

namespace sys {
enum class EventType : uint8_t {
    None = 0, ScreenOn, ScreenOff, PowerOff, VolumeChanged, BrightnessChanged,
    BackRequested, HomeRequested, WifiConnecting, WifiConnected,
    WifiDisconnected, WifiFailed, BluetoothToggled, TimeSynced,
    PreviousPage, NextPage,
};
struct Event {
    EventType type = EventType::None;
    int32_t i32 = 0;
};
using EventHandler = std::function<void(const Event &)>;

// Producers may publish from any TASK (not ISR). Only the GUI task may
// subscribe/unsubscribe/dispatch. No hardware callback ever executes LVGL.
class EventBus {
public:
    static bool init();
    static uint32_t subscribe(EventType type, EventHandler handler);
    static void unsubscribe(uint32_t id);
    static void publish(const Event &event);
    static void dispatch(unsigned budget = 8,bool (*allow)(const Event &)=nullptr);
    static uint32_t dropped();
};
} // namespace sys
