#pragma once
#include "esp_err.h"
#include "esp_wifi_types.h"
#include <cstdint>
#include <string>

namespace sys {
enum class WifiState { Off, Disconnected, Searching, Connecting, Connected, Failed };
enum class WifiScanState { Idle, Scanning, Complete, Failed };
enum class WifiConnectPhase { None, Joining, Address, Online };
struct WifiSnapshot {
    bool enabled = false;
    bool transitioning = false;
    WifiState state = WifiState::Off;
    WifiScanState scan = WifiScanState::Idle;
    // Retain the last phase on failure so association and DHCP are distinguishable.
    WifiConnectPhase phase = WifiConnectPhase::None;
    std::string ssid;
    std::string ip;
    int rssi = 0;
    int error = 0;
    esp_err_t scanError = ESP_OK;
    unsigned retry = 0;
    bool retrying = false;
    bool scanPending = false;
    uint32_t revision = 0;
    uint32_t scanRevision = 0;
};
struct HotspotUplink {
    // IPv4 fields use the network byte order of esp_netif/lwIP.
    uint32_t ip=0,netmask=0,gateway=0,dns=0;
    bool online=false;
};
// Mutating methods are SYSTEM SERVICE TASK only. GUI uses SystemService
// requests and immutable, mutex-protected snapshots/results.
class Connectivity {
public:
    static constexpr uint16_t kMaxNetworks = 24;
    // Keep an enabled STA driver for AP+STA; otherwise hand off to AP-only.
    static constexpr esp_err_t HotspotBusy=0x10002;
    static constexpr esp_err_t NetworkNotSeen=0x10003; // presence scans, distinct from driver association failure
    static esp_err_t acquireForHotspot(); // returns NOT_FINISHED until STA_STOP
    static void releaseFromHotspot(); // only after AP driver/netif teardown
    static bool hotspotReserved();
    static bool hotspotUsesStation(); // worker-only borrowed driver lifetime
    static HotspotUplink hotspotUplink();
    static esp_err_t useStationRoute();
    static esp_err_t init(); // lazy, SystemService only
    static esp_err_t setWifiEnabled(bool enabled);
    static esp_err_t stopWifi();
    static esp_err_t disconnectWifi();
    static esp_err_t connectWifi(const std::string &ssid,const std::string &password);
    static esp_err_t validateCredentials(const std::string &ssid,const std::string &password);
    static void poll();
    static esp_err_t startScan();
    static void cancelScan();
    static void cancelUserScan(); // never cancels a connect-owned presence probe
    static WifiSnapshot snapshot();
    static uint16_t scanResults(wifi_ap_record_t *out,uint16_t capacity);
    static bool wifiEnabled();
    static WifiState wifiState();
    static std::string wifiSsid();
    static int wifiRssi();
};
} // namespace sys
