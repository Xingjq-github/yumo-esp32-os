#pragma once
#include "esp_err.h"
#include "theme_model.hpp"
namespace sys {
class Power {
public:
    static void init();
    static void screenOn();
    static void screenOff();
    static void toggleScreen();
    static bool isScreenOn();
    static void activity(); // real user input only; safe from task context
    static uint32_t activityGeneration(); // read-only, no timer/radio side effects
    static void pollAutomatic(uint32_t hints); // GUI task
    static bool saving();
    static bool slowIdle();
    static bool audioMayRun();
    static void automaticScreenOff();
    static void restoreAutomaticScreen();
    static bool automaticSleepAllowed();
    static bool automaticDeepSleepAllowed();
    static bool automaticScreenOffAllowed();
    static void setEconomy(bool low,bool dim,bool allowLightSleep);
    static void processEconomy(); // service worker; serialized radio changes
    // Hide only the physical backlight; keep touch, apps and CPU active.
    static void setDisplayCast(bool active);
    // CPU-only lease for work that must retain normal speed while the display
    // is dark. Does not change brightness, radio preferences or sleep state.
    class WorkGuard {
    public:
        WorkGuard();
        ~WorkGuard();
        WorkGuard(const WorkGuard &)=delete;
        WorkGuard &operator=(const WorkGuard &)=delete;
    private:
        bool acquired=false;
    };
    static void applyBrightness(int percent);
    // GUI app override; never changes Settings/NVS. Screen-off always wins.
    static void setTransientBrightness(int percent);
    static void clearTransientBrightness();
    // Settings and Sleep App share this entry. Deep sleep requires two PWR
    // clicks; normal modem-sleep screen-off still wakes with one click.
    // Requests are non-blocking; SystemService owns all peripheral transitions.
    static esp_err_t deepSleep(bool automatic=false);
    // Worker-only. Reuses GUI/storage drain, but never the deep-sleep timeout.
    static esp_err_t restartForTheme(themes::Id id);
    static esp_err_t shutdown(); // Normal shutdown: drain apps/storage before dropping BAT_EN.
    static bool takeShutdownError();
    static bool takeThemeRestartError(); // GUI consumes a failed/cancelled request
    static void standby(bool automatic=false); // prepare/cancel before terminal deep-sleep entry
    static bool sleepPending(); // preparing, suspended or restoring: block UI commands
    static bool inStandby();
    static bool sleepTerminal();
    static bool sleepUiReady();
    static void acknowledgeSleepUi(); // GUI: apps/input have stopped
    static void processStandby();     // worker; always returns, including while asleep
    static bool wokeFromSleep();
    static void syncTouch();          // GUI only
    // Emergency GPIO path, independent of queues, NVS and GUI.
    static void powerOff();
};
} // namespace sys
