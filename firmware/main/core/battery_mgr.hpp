#pragma once
#include "battery_estimator.hpp"
#include "supply_policy.hpp"
namespace sys {
class Battery {
public:
    static bool init();
    static bool available();
    // Boot / system worker only. ADC every 5s lit / 15s dark and on screen-on.
    static void poll(bool screenOn);
    static void sample();
    // Short hardware evidence check (GPIO + cached USB status), no ADC/I2C.
    // GUI and worker may both call; shared sampling is limited to once per second.
    static void pollSupply(bool force=false);
    // Readers copy one coherent result; they never touch GPIO or ADC.
    static BatterySnapshot snapshot();
    static float voltage();
    static int level();
    static supply::State supplyState();
    static bool supplyProtected();
    // Consumed at the next supply poll; live USB/CHG always overrides confirmation.
    static void confirmBatterySupply();
    static bool canConfirmBatterySupply();
    static void clearSupplyConfirmation();
private:
    Battery()=delete;
};
} // namespace sys
