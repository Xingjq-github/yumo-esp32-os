#pragma once
namespace sys {
class ClockDiscipline {
public:
    // After Settings::load/clock restoration, before rendering the first frame.
    static void boot();
    // Internal-stack sleep executor, immediately before esp_deep_sleep_start.
    static void prepareSleep();
    // Service worker only, AFTER esp_netif_sntp_deinit has fenced TCP/IP callbacks.
    static void networkStopped();
    static void manualTimeChanged();
};
}
