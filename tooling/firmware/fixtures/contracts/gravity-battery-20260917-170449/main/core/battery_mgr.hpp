#pragma once
#include "battery_estimator.hpp"
namespace sys {
class Battery {
public:
    static bool init();
    static bool available();
    // Boot / system worker only. poll observes CHG every second, samples ADC
    // every 5s lit / 15s dark and immediately on return to the lit screen.
    static void poll(bool screenOn);
    static void sample();
    // Readers copy one coherent result; they never touch GPIO or ADC.
    static BatterySnapshot snapshot();
    static float voltage();
    static int level();
private:
    Battery()=delete;
};
} // namespace sys
