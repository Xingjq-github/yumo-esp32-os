#pragma once
#include "ble_hid.hpp"
#include "ble_connection_flow.hpp"
#include "esp_err.h"
#include <array>
#include <cstdint>
#include <string>
namespace sys {
enum class ServiceDomain : uint8_t { Wifi, Bluetooth, System };
enum class ServiceAction : uint8_t {
    WifiEnable, WifiConnect, WifiDisconnect, WifiScan, WifiForget,
    BluetoothEnable, BluetoothDiscover, BluetoothSecureDiscover, BluetoothScan, BluetoothDisconnect, BluetoothConnect, BluetoothReconnect,
    BluetoothForget, BluetoothForgetAll, BluetoothMedia, PairAnswer, SyncTime, Motion, Save, PowerOff, DeepSleep, ThemeRestart, SetTime,
};
struct ServiceResult { ServiceAction action=ServiceAction::Save; esp_err_t error=ESP_OK; uint32_t revision=0; };
enum class TimeSyncState : uint8_t { Idle, Running, Synced, Failed, Offline, Cancelled, Manual };
struct TimeSyncSnapshot {
    TimeSyncState state=TimeSyncState::Idle;
    esp_err_t error=ESP_OK;
    uint32_t revision=0;
    uint8_t server=0;
    int64_t epoch=0;
};
struct BluetoothSnapshot {
    bool enabled=false, full=false, waitingForPeer=false;
    int error=0;
    BleHidState state=BleHidState::Disabled;
    BleBond peer;
    BluetoothOperation operation=BluetoothOperation::None;
    BleBond target; // selected identity, also retained for error attribution
    bool switching=false;
    BleScanState scan=BleScanState::Idle;
    BlePairAction pairing=BlePairAction::None;
    uint32_t passkey=0, pairGeneration=0, discoverableSeconds=0, scanRevision=0, bondRevision=0;
    std::array<BleBond,BlePeerStore::Capacity> bonds{};
    int bondCount=0;
    bool discovering() const { return operation==BluetoothOperation::Discover; }
    bool connecting() const { return operation==BluetoothOperation::Saved || operation==BluetoothOperation::Keyboard || switching; }
    bool cancellable() const { return operation!=BluetoothOperation::None || state==BleHidState::Connecting; }
    bool hasError() const {
        if(operation!=BluetoothOperation::None) return false;
        return state==BleHidState::Failed || (enabled && error!=0 &&
            state!=BleHidState::Connected && state!=BleHidState::Connecting && state!=BleHidState::Starting);
    }
};
// One long-lived PSRAM-backed worker serializes radio, codec and NVS work.
// Requests contain VALUES only: never LVGL pointers or callbacks to an app.
class SystemService {
public:
    // App-local error, distinct from driver/allocation ESP_ERR_NO_MEM.
    static constexpr esp_err_t QueueFull=0x10001;
    static const char *errorText(esp_err_t error); // nullptr for unclassified codes
    static bool start();
    static bool running();
    static void wake();
    static ServiceDomain domain(ServiceAction action);
    static esp_err_t request(ServiceAction action,bool value=false);
    static esp_err_t switchTheme(uint8_t id);
    static esp_err_t connectWifi(const std::string &ssid,const std::string &password);
    static esp_err_t forgetWifi(const std::string &ssid);
    static esp_err_t forgetBond(const BleBond &bond);
    static esp_err_t connectKeyboard(const BleScanDevice &device);
    static esp_err_t reconnectBond(const BleBond &device);
    static esp_err_t sendConsumer(ConsumerKey key);
    static esp_err_t answerPairing(bool accept,uint32_t expectedGeneration);
    static void cancelBluetoothScan();
    // GUI-safe, queue-independent. Invalidates queued connection intents and
    // cancels incomplete invitations; established links/bonds remain untouched.
    static void cancelBluetoothConnection();
    static void cancelWifiScan();
    static void setVolume(int value); // latest value wins; does not flood a queue
    static esp_err_t setClock(int64_t epoch); // serialized with SNTP; manual value wins
    static bool timeSyncBusy();
    static TimeSyncSnapshot timeSync();
    static bool busy(ServiceDomain domain);
    static ServiceResult result(ServiceDomain domain);
    static BluetoothSnapshot bluetooth();
    static int nearby(BleScanDevice *out,int capacity);
};
} // namespace sys
