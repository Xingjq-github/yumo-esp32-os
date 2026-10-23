#pragma once
#include "lan_hotspot_model.hpp"
#include "esp_err.h"
#include <array>
#include <string>

namespace sys {
enum class LanPhase { Off, Preparing, Starting, On, Stopping, Failed };
struct LanClient {
    uint8_t mac[6]{};
    char ip[16]{};
    int rssi=0;
    uint32_t uploadBps=0,downloadBps=0;
};
struct LanSnapshot {
    LanPhase phase=LanPhase::Off;
    bool wanted=false,uploadAvailable=false,downloadAvailable=false;
    bool sharing=false,upstreamConnected=false;
    bool webReady=false;
    esp_err_t webError=ESP_OK;
    esp_err_t sharingError=ESP_OK;
    esp_err_t error=ESP_OK;
    unsigned count=0;
    std::array<LanClient,lan::MaxClients> clients{};
    char ip[16]{};
    bool busy() const { return phase==LanPhase::Preparing || phase==LanPhase::Starting || phase==LanPhase::Stopping; }
};
// A foreground lease, not an app-owned worker. Hardware changes run ONLY on
// SystemService. Closing is queue-independent, so a full command queue cannot
// leave a hotspot running after its App and LVGL objects have been destroyed.
class LanHotspot {
public:
    static uint32_t open();
    static void close(uint32_t session);
    static esp_err_t enable(uint32_t session,const std::string &ssid,const std::string &password);
    static void disable(uint32_t session);
    static LanSnapshot snapshot();
    static void poll(); // SystemService only; browser service drains before radio cleanup
    static bool idle();
};
} // namespace sys
