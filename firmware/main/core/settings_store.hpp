/*
 * Central settings storage backed by NVS (namespace "settings").
 *
 * All persisted user preferences live here so the UI never touches NVS keys
 * directly. Values are mutex-protected in RAM; the system service batches NVS commits.
 */
#pragma once

#include "lan_hotspot_model.hpp"
#include "theme_model.hpp"
#include "power_policy.hpp"
#include "app_order_model.hpp"
#include "clock_drift_model.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace sys {

class Settings {
public:
    // Load all values from NVS into the RAM cache. Call once at startup.
    static void load(void);

    // Worker-only: one batched commit after 500 ms idle (or at most 5 s).
    static bool flush(bool force = false);
    static themes::Id theme();
    // Worker-only transaction. A failed save is NOT queued for a later retry.
    static bool saveTheme(themes::Id id);
    // Monotonic high score: GUI updates RAM; the existing worker batches NVS.
    static uint32_t dinoHighScore();
    static void recordDinoScore(uint32_t score);
    static app_order::Order appOrder();
    static bool setAppOrder(const app_order::Order &order);
    static app_order::SaveState appOrderSaveState();
    static void forgetWifi(const std::string &ssid);

    // Brightness 5..100 percent; zero is reserved for screen-off.
    static int  brightness(void);
    static void setBrightness(int value);
    // Preference survives reboot; casting sessions still start disabled.
    static bool castScreenOff();
    static void setCastScreenOff(bool on);
    static power::Config powerConfig();
    static void setPowerConfig(power::Config value);

    // Volume 0..100 (speaker output percentage).
    static int  volume(void);
    static void setVolume(int value);
    // Last successfully played file only; never store playback position.
    static std::string mp3Track();
    static void rememberMp3Track(const std::string &path);

    // Bluetooth HID stack preference. 0/1.
    static bool bluetoothOn(void);
    static void setBluetoothOn(bool on);

    // Wi-Fi radio preference and credentials for optional auto-reconnect.
    static bool        wifiOn(void);
    static void        setWifiOn(bool on);
    static std::string wifiSsid(void);
    static std::string wifiPassword(void);
    static void        setWifiCredentials(const std::string &ssid,
                                          const std::string &password);

    // Wi-Fi connection history: remembers per-SSID passwords so a previously
    // used network can be reconnected without retyping the password.
    struct WifiEntry {
        std::string ssid;
        std::string password;
    };
    // Insert or update an entry (moves it to the front / most recent).
    static void                   rememberWifi(const std::string &ssid,
                                               const std::string &password);
    // Look up a remembered password by SSID. Empty string if unknown.
    static std::string            wifiPasswordFor(const std::string &ssid);
    // Whether the SSID exists in the history.
    static bool                   wifiKnown(const std::string &ssid);
    // Full history, most-recent first.
    static std::vector<WifiEntry> wifiHistory(void);

    // LAN credentials only; hotspot enablement is a foreground lease, never saved.
    static std::string lanSsid();
    static std::string lanPassword();
    static bool setLanCredentials(const std::string &ssid,const std::string &password);

    // System info overlay preference (small floating readout at top-left).
    static bool sysInfoOn(void);
    static void setSysInfoOn(bool on);

    // Motion sensor preference. Disabling it powers down accel/gyro outputs.
    static bool motionSensorOn(void);
    static void setMotionSensorOn(bool on);

    // Wall-clock persistence. The ESP32 has no battery-backed RTC on this
    // board, so system time resets to 0 on every reset/brownout. We snapshot
    // the current epoch to NVS periodically and on explicit time edits, then
    // restore the last saved value at boot (time spent powered off is unknown).
    // saveClock updates the RAM cache; the worker commits it. restoreClock
    // applies that saved value via settimeofday; SNTP refines it when online.
    static void saveClock(int64_t epoch);
    static bool restoreClock(void);
    static clock_drift::Model clockDrift();
    static void saveClockDrift(clock_drift::Model model);

private:
    struct Cache {
        int         brightness = 30;
        int         volume     = 25;
        themes::Id  theme      = themes::Default;
        bool        bluetooth  = true;
        bool        wifi       = true;
        std::string ssid;
        std::string password;
        std::string mp3Track;
        std::string lanSsid = lan::DefaultSsid;
        std::string lanPassword = lan::DefaultPassword;
        bool        sysinfo    = false;
        bool        castScreenOff = false;
        bool        motion     = false;
        bool        loaded     = false;
        int64_t     clockEpoch = 0;
        clock_drift::Model clockDrift;
        uint32_t    dinoBest = 0;
        app_order::Order appOrder;
        uint32_t orderRevision=0;
        app_order::SaveState orderState=app_order::SaveState::Saved;
        power::Config power;
        std::vector<WifiEntry> wifiHist;
    };
    static Cache &cache(void);
};

}// namespace sys
