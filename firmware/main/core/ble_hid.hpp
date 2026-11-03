#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

namespace sys {

enum class BleHidState : uint8_t {
    Disabled,
    Starting,
    Ready,
    Advertising,
    Connecting,
    Connected,
    Failed,
};

// A bond record persisted by the NimBLE store (one paired host).
struct BleBond {
    uint8_t addrType = 0;
    uint8_t addr[6] = {};
    bool    connected = false;
    bool keyboard = false; // Persisted name: true = external HID central role (keyboard/gamepad).
    char name[64] = {};
};

// Nearby advertiser; keyboard connections require explicit selection and HID discovery.
struct BleScanDevice {
    BleBond address;
    BleBond identity; // identity resolved by the SDK/paired IRK; never by name
    char name[64] = {};
    int8_t rssi = 0;
    bool connectable=false, hid=false;
    int64_t seenAtMs=0;
};

enum class BleScanState : uint8_t { Idle, Scanning, Complete, Failed };

// Pending user action during pairing.
enum class BlePairAction : uint8_t {
    None,       // nothing to do
    Confirm,    // show the 6-digit number and ask yes/no (numeric comparison)
    Display,    // show the 6-digit number, the peer types it in
};
struct BlePairRequest {
    BlePairAction action=BlePairAction::None;
    uint32_t passkey=0;
    uint32_t generation=0;
};

enum class ConsumerKey : uint16_t {
    PlayPause = 0x00CD,
    NextTrack = 0x00B5,
    PreviousTrack = 0x00B6,
    Stop = 0x00B7,
    VolumeUp = 0x00E9,
    VolumeDown = 0x00EA,
    Mute = 0x00E2,
};

class BleHid {
public:
    static esp_err_t init(void);
    static esp_err_t setEnabled(bool enabled);

    static void poll(); // SystemService alone owns advertising start/restart.
    static int lastError();
    static esp_err_t prepareDeepSleep(); // terminal only; never frees dynamic GATT
    static bool enabled(void);
    static bool connected(void); // any authenticated, usable link
    static bool keyboardConnected();
    static bool gamepadConnected();
    static bool remoteConnected(); // this device sending HID to a computer/phone
    static BleBond peer();
    static bool waitingForPeer(); // SystemService snapshot only
    static bool connectionIdle(); // includes controller links not yet reported by CONNECT
    static esp_err_t connectKeyboard(const BleScanDevice &device);
    static esp_err_t reconnectBond(const BleBond &device); // one manual attempt, role-aware
    static void cancelUserScan();
    static esp_err_t reconnectKeyboard(const BleBond &device); // scan-before-connect
    static void keyboardDiscoveryFinished(bool success,int error);
    static esp_err_t disconnect(void);
    static BleHidState state(void);
    static const char *deviceName(void);
    static esp_err_t makeDiscoverable(bool authenticated = false);
    static esp_err_t stopDiscoverable(void);
    static uint32_t discoverableSecondsRemaining(void);

    // Bounded, 10-second active BLE scan; snapshots are safe for the UI task.
    static constexpr int kMaxScanDevices = 24;
    static esp_err_t startScan(void);
    static void stopScan(void);
    static BleScanState scanState(void);
    static int scanResults(BleScanDevice *out, int maxCount);

    // USB HID key usage (for example: A = 0x04, Enter = 0x28).
    // Nonblocking bounded queue; ESP_OK means accepted, not host execution.
    static esp_err_t sendKey(uint8_t keycode, uint8_t modifiers = 0);
    static void cancelKeys(); // cancel queued presses; finish all-up on the same link
    static bool keysIdle();
    static esp_err_t takeKeyError();
    // Nonblocking mouse output; adjacent motion is coalesced and paced on the
    // NimBLE host queue. ESP_OK means accepted, not host execution. A bounded
    // overload may saturate motion; press/release transitions stay ordered.
    // expectedLink binds a complete gesture to its starting peer; zero uses
    // the current link for callers with no multi-event gesture.
    static uintptr_t mouseLinkGeneration();
    static esp_err_t sendMouse(int32_t dx, int32_t dy, int32_t wheel = 0,
                               uint8_t buttons = 0, uintptr_t expectedLink = 0);
    static esp_err_t clickMouse(uint8_t button, uintptr_t expectedLink = 0);
    static void cancelMouse(); // discard pending motion, finish same-link all-up
    static bool mouseIdle();
    static esp_err_t takeMouseError();
    // Worker-only: preserves the host-required 40ms press/release interval.
    static esp_err_t sendConsumer(ConsumerKey key);

    // Vault-only output. Runs reports on the NimBLE host queue, binding each
    // press/release to an encrypted 128-bit bonded link generation.
    static uintptr_t privateLinkGeneration();
    static uint32_t beginPrivateInput();
    static void cancelPrivateInput();
    static bool privateInputIdle();
    static esp_err_t sendPrivateKey(uint8_t keycode,uint8_t modifiers,uintptr_t link,uint32_t permit);

    // ---- bond management (paired hosts) ----
    // Maximum bonds the NimBLE store can keep (CONFIG_BT_NIMBLE_MAX_BONDS).
    static int maxBonds(void);
    // Enumerate bonds. Returns the number written into out (<= maxCount).
    static int bonds(BleBond *out, int maxCount);
    // Worker-only; last successful connection if still bonded, else latest pairing.
    static esp_err_t bootBond(BleBond &out);
    // Forget one bond: unpair + delete every stored record for that identity.
    static esp_err_t removeBond(const BleBond &bond);
    // Forget every bond.
    static esp_err_t removeAllBonds(void);
    // Set to true when the store refused a new bond because it is full.
    static bool bondStoreFull(void);
    static void clearBondStoreFull(void);

    // ---- pairing confirmation ----
    static BlePairRequest pairing(void);
    // Only the matching request generation may be answered, even if two
    // different attempts happen to display the same six-digit passkey.
    static esp_err_t confirmPairing(bool accept, uint32_t expectedGeneration);

    // Human-readable "aa:bb:cc:dd:ee:ff" into a caller-supplied buffer (>=18).
    static void formatAddr(const BleBond &bond, char *out, size_t outSize);

    // Internal callbacks used by the NimBLE/HID C callback shims.
    static void setStateFromCallback(BleHidState state);
    static void setConnectionHandle(uint16_t handle);
    // Compatibility snapshot only; the handle can expire immediately. New
    // connection operations must go through SystemService's connection flow.
    static uint16_t connectionHandle(void);
    static void startAdvertisingFromCallback(void);
};

}// namespace sys
