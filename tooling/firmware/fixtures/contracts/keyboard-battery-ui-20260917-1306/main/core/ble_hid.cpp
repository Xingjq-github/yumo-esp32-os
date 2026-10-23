#include "ble_hid.hpp"
#include "battery_mgr.hpp"

#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "esp_bt.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" void ble_store_config_init(void);

namespace sys {
namespace {

constexpr char kTag[] = "ble_hid";
constexpr char kDeviceName[] = "Brookesia-s3";
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kMouseReportId = 2;
constexpr uint8_t kConsumerReportId = 3;
constexpr uint16_t kInvalidHandle = BLE_HS_CONN_HANDLE_NONE;
constexpr int64_t kDiscoverableDurationUs = 60LL * 1000 * 1000;
#ifdef CONFIG_BT_NIMBLE_MAX_BONDS
constexpr int kMaxBondSlots = CONFIG_BT_NIMBLE_MAX_BONDS;
#else
constexpr int kMaxBondSlots = 3;
#endif

std::atomic<BleHidState> s_state{BleHidState::Disabled};
std::atomic<bool> s_enabled{false};
std::atomic<bool> s_initialized{false};
std::atomic<bool> s_hostRunning{false};
StaticTask_t s_hostTaskControl; // TCB must remain in internal RAM.
StackType_t *s_hostStack=nullptr; // Boot-lifetime allocation, retained across radio Off/On.
std::atomic<int64_t> s_startDeadlineUs{0};
constexpr int64_t kHostStartTimeoutUs=10000000;
// With flash instructions/rodata mapped to PSRAM, flash operations leave the
// external-memory cache available. Use the same explicit-stack policy as the
// system worker; otherwise retain an internal stack for cache-off safety.
#if CONFIG_SPIRAM_XIP_FROM_PSRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
constexpr uint32_t kHostStackCaps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
#else
constexpr uint32_t kHostStackCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
#endif
std::atomic<bool> s_advPending{false}, s_advRestart{false};
std::atomic<int> s_lastError{0}, s_pairError{0};
int64_t s_nextAdvAttempt=0;
std::atomic<int64_t> s_securityDeadlineUs{0};
void processAdvertising();
// Advertising permission. True whenever BLE HID is on and no host is
// connected. It must NOT be cleared when the discoverable window ends,
// otherwise a bonded phone can never reconnect after its Bluetooth is
// switched off and on again.
std::atomic<bool> s_advertisingAllowed{false};
// Discoverable window. Only selects general-discovery advertising (visible in
// scan lists). Outside the window we keep advertising connectable but
// non-discoverable, which is what bonded hosts use to reconnect.
std::atomic<bool> s_discoverable{false};
std::atomic<int64_t> s_discoverableDeadlineUs{0};
std::atomic<uint16_t> s_connHandle{kInvalidHandle};
std::atomic<bool> s_disconnectPending{false};
std::atomic<uint8_t> s_ownAddrType{BLE_OWN_ADDR_PUBLIC};
portMUX_TYPE s_pairLock = portMUX_INITIALIZER_UNLOCKED;
BlePairRequest s_pairRequest;
uint16_t s_pairConnHandle=kInvalidHandle;
std::atomic<bool> s_bondStoreFull{false};
std::atomic<esp_hidd_dev_t *> s_device{nullptr};
esp_timer_handle_t s_discoverableTimer = nullptr;
std::atomic<BleScanState> s_scanState{BleScanState::Idle};
// Task-side only; recursive because setEnabled() calls init()/stopScan().
// Keep scan commands out of host teardown. Callbacks never take this mutex.
std::recursive_mutex s_scanControlMutex;
portMUX_TYPE s_scanLock = portMUX_INITIALIZER_UNLOCKED;
std::uintptr_t s_scanGeneration = 0; // Protected by s_scanLock; never reused.
BleScanDevice s_scanDevices[BleHid::kMaxScanDevices];
int s_scanCount = 0;

int scanEvent(struct ble_gap_event *event, void *arg)
{
    // The argument is a value, not a pointer to mutable per-scan storage.
    const auto generation = reinterpret_cast<std::uintptr_t>(arg);
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        portENTER_CRITICAL(&s_scanLock);
        if (generation == s_scanGeneration &&
            s_scanState.load() == BleScanState::Scanning) {
            s_scanState.store(event->disc_complete.reason == 0 ? BleScanState::Complete
                                                             : BleScanState::Failed);
        }
        portEXIT_CRITICAL(&s_scanLock);
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC ||
        s_scanState.load() != BleScanState::Scanning) return 0;

    const auto &disc = event->disc;
    struct ble_hs_adv_fields fields = {};
    const bool parsed = ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) == 0;
    // Parse/copy outside the critical section; the host must never touch LVGL.
    char name[64] = {};
    if (parsed && fields.name && fields.name_len) {
        size_t n = fields.name_len;
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        // Do not leave a partial UTF-8 code point when truncating a long name.
        if (n < fields.name_len) {
            while (n && (fields.name[n] & 0xc0) == 0x80) --n;
        }
        std::memcpy(name, fields.name, n);
        for (size_t i = 0; i < n; ++i) {
            if (static_cast<unsigned char>(name[i]) < 0x20) name[i] = ' ';
        }
    }
    portENTER_CRITICAL(&s_scanLock);
    // Cancellation/new start may have happened while this report was parsed.
    if (generation != s_scanGeneration ||
        s_scanState.load() != BleScanState::Scanning) {
        portEXIT_CRITICAL(&s_scanLock);
        return 0;
    }
    int index = 0;
    for (; index < s_scanCount; ++index) {
        if (s_scanDevices[index].address.addrType == disc.addr.type &&
            std::memcmp(s_scanDevices[index].address.addr, disc.addr.val, 6) == 0) break;
    }
    if (index < BleHid::kMaxScanDevices) {
        auto &device = s_scanDevices[index];
        if (index == s_scanCount) {
            device = {};
            device.address.addrType = disc.addr.type;
            std::memcpy(device.address.addr, disc.addr.val, 6);
            ++s_scanCount;
        }
        device.rssi = disc.rssi;
        if (name[0]) std::memcpy(device.name, name, sizeof(name));
    }
    portEXIT_CRITICAL(&s_scanLock);
    return 0;
}

void clearPairRequest(void)
{
    portENTER_CRITICAL(&s_pairLock);
    s_pairRequest.action=BlePairAction::None;
    s_pairRequest.passkey=0;
    s_pairConnHandle=kInvalidHandle;
    portEXIT_CRITICAL(&s_pairLock);
}
void publishPairRequest(BlePairAction action,uint32_t passkey,uint16_t handle)
{
    portENTER_CRITICAL(&s_pairLock);
    if(++s_pairRequest.generation==0) ++s_pairRequest.generation;
    s_pairRequest.action=action;
    s_pairRequest.passkey=passkey;
    s_pairConnHandle=handle;
    portEXIT_CRITICAL(&s_pairLock);
}

// Refuse a new bond when the store is full instead of silently evicting the
// oldest one (the NimBLE sample callback ble_store_util_status_rr does that).
// The UI polls bondStoreFull() and asks the user to delete a bond first.
int bondStoreStatus(struct ble_store_status_event *event, void *)
{
    switch (event->event_code) {
    case BLE_STORE_EVENT_OVERFLOW:
        switch (event->overflow.obj_type) {
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
        case BLE_STORE_OBJ_TYPE_PEER_ADDR:
            s_bondStoreFull.store(true);
            ESP_LOGW(kTag, "Bond store full; refusing new bond (delete one first)");
            return BLE_HS_ESTORE_CAP;
        default:
            // CCCD/CSFC and friends are per-connection state, not identities.
            // Evicting the oldest *other* peer is acceptable there.
            return ble_store_util_status_rr(event, nullptr);
        }
    case BLE_STORE_EVENT_FULL:
        return 0;
    default:
        return BLE_HS_EUNKNOWN;
    }
}

void cancelDiscoverableTimeout(void)
{
    s_discoverable.store(false);
    s_discoverableDeadlineUs.store(0);
    if (s_discoverableTimer) (void)esp_timer_stop(s_discoverableTimer);
}

void discoverableTimeout(void *)
{
    // Timer context only publishes intent. No GAP start/stop races here.
    s_discoverable.store(false); s_discoverableDeadlineUs.store(0);
    s_advRestart.store(true); s_advPending.store(true);
}

esp_err_t initDiscoverableTimer(void)
{
    esp_timer_create_args_t args = {};
    args.callback = discoverableTimeout;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "ble_disc";
    return esp_timer_create(&args, &s_discoverableTimer);
}

// One report map exposes keyboard, mouse and consumer-control collections.
const uint8_t kReportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, kKeyboardReportId,
    0x05, 0x07,       // Usage Page (Keyboard)
    0x19, 0xE0,       // Usage Minimum (Left Control)
    0x29, 0xE7,       // Usage Maximum (Right GUI)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,       // Input (Data, Variable, Absolute)
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,       // Reserved byte
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,       // Usage Page (LEDs)
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,       // Output (LEDs)
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,       // Six key usages
    0xC0,

    0x05, 0x01,
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,
    0x85, kMouseReportId,
    0x09, 0x01,
    0xA1, 0x00,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x03,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x03,
    0x75, 0x01,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x01,
    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x38,
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x03,
    0x81, 0x06,
    0xC0,
    0xC0,

    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,
    0x85, kConsumerReportId,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,
    0xC0,
};

esp_hid_raw_report_map_t s_reportMaps[] = {
    {kReportMap, sizeof(kReportMap)},
};

esp_hid_device_config_t s_deviceConfig = {
    .vendor_id = 0x303A,
    .product_id = 0x4001,
    .version = 0x0100,
    .device_name = kDeviceName,
    .manufacturer_name = "Espressif",
    .serial_number = "BKHID001",
    .report_maps = s_reportMaps,
    .report_maps_len = 1,
};

struct ble_hs_adv_fields s_advFields;
ble_uuid16_t s_hidUuid = BLE_UUID16_INIT(0x1812);

void hostTask(void *)
{
    s_hostRunning.store(true);
    ESP_LOGI(kTag,"NimBLE host task started stack=%u caps=0x%lx",
             CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,static_cast<unsigned long>(kHostStackCaps));
    nimble_port_run();
    s_hostRunning.store(false);
    if(s_enabled.load()) {
        s_lastError.store(ESP_ERR_INVALID_STATE);s_state.store(BleHidState::Failed);
        ESP_LOGE(kTag,"NimBLE host task stopped unexpectedly");
    }
    // Static task storage lives until reset. No temporary cleanup task/extra
    // internal stack is allocated on the low-memory deep-sleep terminal path.
    vTaskDelete(nullptr);
}

int gapEvent(struct ble_gap_event *event, void *)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(kTag, "GAP connect status=%d handle=%u", event->connect.status,
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            BleHid::setConnectionHandle(event->connect.conn_handle);
            if (!s_enabled.load()) {
                s_disconnectPending.store(true);
                // A connection that raced with switching Bluetooth off.
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            // End the visible window, but keep the advertising permission so
            // advertising resumes by itself after a disconnect.
            cancelDiscoverableTimeout();
            s_lastError.store(0); s_pairError.store(0);
            s_securityDeadlineUs.store(esp_timer_get_time()+30000000);
            BleHid::setStateFromCallback(BleHidState::Connecting);
            const int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(kTag, "Cannot initiate pairing: rc=%d", rc);
                s_pairError.store(rc); s_state.store(BleHidState::Failed);
                (void)ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
            }
        } else if (s_enabled.load() && s_advertisingAllowed.load()) {
            BleHid::startAdvertisingFromCallback();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(kTag, "GAP disconnect reason=%d", event->disconnect.reason);
        if(event->disconnect.conn.conn_handle!=s_connHandle.load()) return 0;
        s_disconnectPending.store(false); s_securityDeadlineUs.store(0);
        BleHid::setConnectionHandle(kInvalidHandle);
        BleHid::setStateFromCallback(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
        clearPairRequest();
        if (s_enabled.load() && s_advertisingAllowed.load()) BleHid::startAdvertisingFromCallback();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(kTag, "GAP encryption changed status=%d handle=%u",
                 event->enc_change.status, event->enc_change.conn_handle);
        clearPairRequest();
        if(event->enc_change.conn_handle!=s_connHandle.load() || !s_enabled.load() || s_disconnectPending.load()) return 0;
        if (event->enc_change.status == BLE_HS_ESTORE_CAP) s_bondStoreFull.store(true);
        if(event->enc_change.status==0) {
            struct ble_gap_conn_desc desc{};
            if(ble_gap_conn_find(event->enc_change.conn_handle,&desc)==0 && desc.sec_state.encrypted) {
                s_securityDeadlineUs.store(0); s_pairError.store(0); s_state.store(BleHidState::Connected);
            }
        } else {
            s_pairError.store(event->enc_change.status);
            s_state.store(BleHidState::Failed);
            (void)ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        const uint8_t action = event->passkey.params.action;
        if (action == BLE_SM_IOACT_NUMCMP) {
            // Secure Connections numeric comparison: show the number and let
            // the user accept or reject it from the Bluetooth screen.
            publishPairRequest(BlePairAction::Confirm,event->passkey.params.numcmp,event->passkey.conn_handle);
            ESP_LOGI(kTag, "Pairing confirm requested: %06lu",
                     static_cast<unsigned long>(event->passkey.params.numcmp));
        } else if (action == BLE_SM_IOACT_DISP) {
            // Legacy pairing: we pick the passkey and display it; the peer
            // types it in. Inject it right away, the UI just shows it.
            struct ble_sm_io io = {};
            io.action = action;
            io.passkey = esp_random() % 1000000;
            const int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
            if (rc != 0) {
                ESP_LOGW(kTag, "Cannot inject passkey: rc=%d", rc);
                clearPairRequest();
            } else {
                publishPairRequest(BlePairAction::Display,io.passkey,event->passkey.conn_handle);
                ESP_LOGI(kTag, "Pairing passkey to enter on the host: %06lu",
                         static_cast<unsigned long>(io.passkey));
            }
        } else {
            ESP_LOGW(kTag, "Unsupported passkey action=%u", action);
            clearPairRequest();
        }
        return 0;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_enabled.load() && s_advertisingAllowed.load() && !BleHid::connected()) {
            BleHid::startAdvertisingFromCallback();
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ESP_LOGW(kTag, "GAP repeat pairing handle=%u; deleting old bond",
                 event->repeat_pairing.conn_handle);
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            const int rc = ble_store_util_delete_peer(&desc.peer_id_addr);
            if (rc != 0) {
                ESP_LOGE(kTag, "Cannot delete old bond: rc=%d", rc);
            }
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        return 0;
    }
}

esp_err_t configureGap(void)
{
    std::memset(&s_advFields, 0, sizeof(s_advFields));
    s_advFields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_advFields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    s_advFields.appearance_is_present = 1;
    s_advFields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_advFields.tx_pwr_lvl_is_present = 1;
    s_advFields.name = reinterpret_cast<uint8_t *>(const_cast<char *>(kDeviceName));
    s_advFields.name_len = std::strlen(kDeviceName);
    s_advFields.name_is_complete = 1;
    s_advFields.uuids16 = &s_hidUuid;
    s_advFields.num_uuids16 = 1;
    s_advFields.uuids16_is_complete = 1;

    // Just Works pairing: NO_IO + no MITM lets iOS/Android complete the LE
    // Secure Connections handshake without any on-screen confirmation, which
    // is how commercial BLE keyboards/mice behave. Fixes the case where the
    // numeric-comparison dialog only lived on the Bluetooth settings page.
    // Installed before HID profile init so its own sync/reset callback chain is retained.
    ble_hs_cfg.sync_cb=[]() {
        ESP_LOGI(kTag,"NimBLE host synchronized");
        s_startDeadlineUs.store(0);s_lastError.store(0);
        if(s_enabled.load()) s_advPending.store(true);
    };
    ble_hs_cfg.reset_cb=[](int reason) {
        ESP_LOGW(kTag,"NimBLE host reset reason=%d",reason);
        s_startDeadlineUs.store(esp_timer_get_time()+kHostStartTimeoutUs);
        s_disconnectPending.store(false); s_securityDeadlineUs.store(0);
        s_connHandle.store(kInvalidHandle); clearPairRequest();
        s_lastError.store(reason); s_advPending.store(true);
        s_state.store(s_enabled.load()?BleHidState::Starting:BleHidState::Disabled);
    };
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    return ESP_OK;
}

void hiddEvent(void *, esp_event_base_t, int32_t id, void *)
{
    switch (static_cast<esp_hidd_event_t>(id)) {
    case ESP_HIDD_START_EVENT:
        if (s_enabled.load() && s_advertisingAllowed.load()) BleHid::startAdvertisingFromCallback();
        break;
    case ESP_HIDD_CONNECT_EVENT: {
        auto *device = s_device.load();
        if (!s_enabled.load() || !device) break;
        // GAP owns connection state; HID notifications can arrive later.
        const int level = Battery::level();
        if (level >= 0) {
            const esp_err_t err = esp_hidd_dev_battery_set(device, static_cast<uint8_t>(level));
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Cannot publish battery level: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(kTag, "Battery level published: %d%%", level);
            }
        } else {
            ESP_LOGW(kTag, "Battery level unavailable");
        }
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT:
        // GAP already cleared the handle and resumed advertising. A delayed
        // HID notification must not clear a newer connection or start it twice.
        break;
    default:
        break;
    }
}

esp_err_t sendReport(uint8_t reportId, uint8_t *data, size_t length)
{
    auto *device = s_device.load();
    if (!BleHid::connected() || !device || !esp_hidd_dev_connected(device)) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_hidd_dev_input_set(device, 0, reportId, data, length);
}

} // namespace

esp_err_t BleHid::init(void)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if (s_initialized.load()) return ESP_OK;
    s_state.store(BleHidState::Starting);

    esp_hidd_dev_t *device = nullptr;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) goto fail;

    configureGap();
    err = esp_hidd_dev_init(&s_deviceConfig, ESP_HID_TRANSPORT_BLE, hiddEvent, &device);
    if (err != ESP_OK) {
        (void)nimble_port_deinit();
        goto fail;
    }

    // With CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC, this setter frees/replaces a
    // shared string and is not safe to call concurrently from GAP/HID events.
    // Set it once after GAP service initialization, before starting the host.
    {
        const int rc=ble_svc_gap_device_name_set(kDeviceName);
        if(rc!=0) {
            ESP_LOGE(kTag,"Cannot set BLE device name: rc=%d",rc);
            err=rc==BLE_HS_ENOMEM?ESP_ERR_NO_MEM:ESP_FAIL;
            (void)esp_hidd_dev_deinit(device);
            (void)nimble_port_deinit();
            goto fail;
        }
    }
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = bondStoreStatus;
    err = initDiscoverableTimer();
    if (err != ESP_OK) {
        (void)esp_hidd_dev_deinit(device);
        (void)nimble_port_deinit();
        goto fail;
    }

    s_device.store(device);
    s_initialized.store(true);
    s_startDeadlineUs.store(esp_timer_get_time()+kHostStartTimeoutUs);
    // The SDK void wrapper ignores xTaskCreate failure. A missing host task
    // otherwise looks like Starting forever on a RAM-constrained device.
    s_hostStack=static_cast<StackType_t *>(heap_caps_malloc(CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,kHostStackCaps));
    if(!s_hostStack || !xTaskCreateStaticPinnedToCore(hostTask,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
            nullptr,configMAX_PRIORITIES-4,s_hostStack,&s_hostTaskControl,CONFIG_BT_NIMBLE_PINNED_TO_CORE)) {
        ESP_LOGE(kTag,"Cannot create NimBLE host: internal=%u largest=%u external=%u",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
        heap_caps_free(s_hostStack);s_hostStack=nullptr;
        s_initialized.store(false);s_device.store(nullptr);
        // No host was created: pre-start cleanup cannot race with its callbacks.
        (void)esp_hidd_dev_deinit(device);(void)nimble_port_deinit();
        err=ESP_ERR_NO_MEM;goto fail;
    }
    return ESP_OK;

fail:
    ESP_LOGE(kTag, "BLE HID init failed: %s", esp_err_to_name(err));
    s_lastError.store(err);
    s_initialized.store(false);
    s_startDeadlineUs.store(0);
    s_enabled.store(false);
    s_state.store(BleHidState::Failed);
    return err;
}

esp_err_t BleHid::setEnabled(bool enabled)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if (enabled) {
        const bool wasEnabled=s_enabled.exchange(true);
        if(!wasEnabled || s_state.load()==BleHidState::Failed) {
            s_startDeadlineUs.store(esp_timer_get_time()+kHostStartTimeoutUs);
            s_lastError.store(0);
        }
        s_pairError.store(0);
        esp_err_t err = init();
        if (err != ESP_OK) {
            s_enabled.store(false);
            return err;
        }
        s_advertisingAllowed.store(true); // also restore permission before waiting for an old link
        if(s_connHandle.load()!=kInvalidHandle) {
            if(!wasEnabled) {
                s_state.store(BleHidState::Starting);
                s_securityDeadlineUs.store(esp_timer_get_time()+2000000);
                (void)makeDiscoverable();
            }
            // Includes a link being authenticated or terminated after quick Off/On.
            // Never fabricate Connected or advertise into an occupied slot.
            return ESP_OK;
        }
        // First enable opens a bounded pairing window, including when there are
        // existing bonds. A saved bond is never interpreted as an active link.
        if(!wasEnabled) (void)makeDiscoverable();
        // Queue advertising: the service poll serializes all GAP start/restarts.
        s_advertisingAllowed.store(true);
        if (ble_hs_synced()) {
            s_lastError.store(0); s_state.store(BleHidState::Ready);
            startAdvertisingFromCallback();
        } else {
            s_state.store(BleHidState::Starting);
        }
        return ESP_OK;
    }

    s_enabled.store(false);
    s_startDeadlineUs.store(0);
    clearPairRequest();
    s_advertisingAllowed.store(false);
    s_discoverable.store(false);
    cancelDiscoverableTimeout();
    stopScan();
    if (!s_initialized.load()) {
        s_state.store(BleHidState::Disabled);
        return ESP_OK;
    }

    if (ble_gap_adv_active()) (void)ble_gap_adv_stop();
    // GAP disconnect owns clearing the handle. A quick Off->On must wait for
    // the old link to terminate, not advertise into an occupied connection slot.
    const uint16_t handle = s_connHandle.load();
    if (handle != kInvalidHandle) {
        s_disconnectPending.store(true);
        (void)ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    // Stop the radio activity only. The HID profile and the NimBLE stack stay
    // resident: esp_hidd_dev_deinit() + nimble_port_stop() + nimble_port_deinit()
    // tear down the dynamic GATT tables while host callbacks, the HCI callback
    // and callout timers may still be in flight, which crashed the device.
    // Keeping the stack loaded makes switching Bluetooth off cheap and lets a
    // later enable simply resume advertising.
    s_discoverableDeadlineUs.store(0);
    s_state.store(BleHidState::Disabled);
    return ESP_OK;
}

esp_err_t BleHid::disconnect(void)
{
    const uint16_t handle = s_connHandle.load();
    if (handle == kInvalidHandle) return ESP_ERR_INVALID_STATE;
    return ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t BleHid::startScan(void)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if (!s_initialized.load() || !s_enabled.load() || !ble_hs_synced()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scanState.load() == BleScanState::Scanning) return ESP_OK;
    // A failed cancel can leave discovery active with an invalidated argument.
    // Do not clear its cached snapshot or report a new scan as successful.
    if (ble_gap_disc_active()) {
        portENTER_CRITICAL(&s_scanLock);
        s_scanState.store(BleScanState::Failed);
        portEXIT_CRITICAL(&s_scanLock);
        ESP_LOGW(kTag, "Cannot start BLE scan: previous discovery still active");
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_scanLock);
    // Fail closed on exhaustion rather than ever reusing an old callback token.
    if (s_scanGeneration == UINTPTR_MAX) {
        s_scanState.store(BleScanState::Failed);
        portEXIT_CRITICAL(&s_scanLock);
        ESP_LOGW(kTag, "Cannot start BLE scan: generation exhausted");
        return ESP_FAIL;
    }
    const auto generation = ++s_scanGeneration;
    s_scanCount = 0;
    s_scanState.store(BleScanState::Scanning);
    portEXIT_CRITICAL(&s_scanLock);

    struct ble_gap_disc_params params = {};
    params.passive = 0; // Active scan obtains names carried in scan responses.
    params.itvl = 160;  // 100 ms interval, 30 ms window: leave airtime for HID.
    params.window = 48;
    params.filter_duplicates = 0;
    params.limited = 0;
    const int rc = ble_gap_disc(s_ownAddrType.load(), 10000, &params, scanEvent,
                                reinterpret_cast<void *>(generation));
    if (rc != 0) {
        portENTER_CRITICAL(&s_scanLock);
        if (generation == s_scanGeneration &&
            s_scanState.load() == BleScanState::Scanning) {
            s_scanState.store(BleScanState::Failed);
        }
        portEXIT_CRITICAL(&s_scanLock);
        ESP_LOGW(kTag, "Cannot start BLE scan: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void BleHid::stopScan(void)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    // Invalidate before cancellation: a callback may already hold the old arg.
    portENTER_CRITICAL(&s_scanLock);
    if (s_scanGeneration != UINTPTR_MAX) ++s_scanGeneration;
    s_scanState.store(BleScanState::Idle);
    portEXIT_CRITICAL(&s_scanLock);

    // Cancel does not emit DISC_COMPLETE. Also guard calls after host teardown.
    if (s_initialized.load() && ble_gap_disc_active()) {
        const int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            portENTER_CRITICAL(&s_scanLock);
            s_scanState.store(BleScanState::Failed);
            portEXIT_CRITICAL(&s_scanLock);
            ESP_LOGW(kTag, "Cannot stop BLE scan: rc=%d", rc);
        }
    }
}

BleScanState BleHid::scanState(void) { return s_scanState.load(); }

int BleHid::scanResults(BleScanDevice *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    portENTER_CRITICAL(&s_scanLock);
    const int count = s_scanCount < maxCount ? s_scanCount : maxCount;
    std::memcpy(out, s_scanDevices, count * sizeof(BleScanDevice));
    portEXIT_CRITICAL(&s_scanLock);
    return count;
}

bool BleHid::enabled(void) { return s_enabled.load(); }
bool BleHid::connected(void)
{
    return s_enabled.load() && !s_disconnectPending.load() && s_connHandle.load()!=kInvalidHandle &&
        s_state.load()==BleHidState::Connected;
}
BleHidState BleHid::state(void) { return s_state.load(); }
const char *BleHid::deviceName(void) { return kDeviceName; }

esp_err_t BleHid::makeDiscoverable(void)
{
    if (!s_initialized.load() || !s_enabled.load()) return ESP_ERR_INVALID_STATE;
    if (connected()) return ESP_OK;

    cancelDiscoverableTimeout();
    const int64_t deadline = esp_timer_get_time() + kDiscoverableDurationUs;
    s_discoverableDeadlineUs.store(deadline);
    if (!s_discoverableTimer ||
        esp_timer_start_once(s_discoverableTimer, kDiscoverableDurationUs) != ESP_OK) {
        s_discoverableDeadlineUs.store(0);
        return ESP_FAIL;
    }

    s_discoverable.store(true);
    s_advertisingAllowed.store(true);
    if (!ble_hs_synced()) {
        s_state.store(BleHidState::Starting);
        return ESP_OK;
    }
    s_advRestart.store(true); s_advPending.store(true);
    return ESP_OK;
}

esp_err_t BleHid::stopDiscoverable(void)
{
    cancelDiscoverableTimeout();
    s_advRestart.store(true); s_advPending.store(true);
    return ESP_OK;
}

uint32_t BleHid::discoverableSecondsRemaining(void)
{
    const int64_t deadline = s_discoverableDeadlineUs.load();
    if (!s_discoverable.load() || deadline <= 0) return 0;
    const int64_t remaining = deadline - esp_timer_get_time();
    if (remaining <= 0) return 0;
    return static_cast<uint32_t>((remaining + 999999) / 1000000);
}

esp_err_t BleHid::sendKey(uint8_t keycode, uint8_t modifiers)
{
    uint8_t pressed[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
    esp_err_t err = sendReport(kKeyboardReportId, pressed, sizeof(pressed));
    if (err != ESP_OK) return err;
    uint8_t released[8] = {};
    return sendReport(kKeyboardReportId, released, sizeof(released));
}

esp_err_t BleHid::sendMouse(int8_t dx, int8_t dy, int8_t wheel, uint8_t buttons)
{
    uint8_t report[4] = {buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy),
                         static_cast<uint8_t>(wheel)};
    return sendReport(kMouseReportId, report, sizeof(report));
}

esp_err_t BleHid::clickMouse(uint8_t button)
{
    esp_err_t err = sendMouse(0, 0, 0, button);
    if (err != ESP_OK) return err;
    return sendMouse(0, 0, 0, 0);
}

esp_err_t BleHid::sendConsumer(ConsumerKey key)
{
    uint16_t usage = static_cast<uint16_t>(key);
    uint8_t pressed[2] = {static_cast<uint8_t>(usage), static_cast<uint8_t>(usage >> 8)};
    esp_err_t err = sendReport(kConsumerReportId, pressed, sizeof(pressed));
    if (err != ESP_OK) return err;
    // Keep the "pressed" report alive briefly before releasing. Some media apps
    // (e.g. NetEase Cloud Music) drive their media-session listener on real key
    // dwell time and will drop a press that is released in the same instant,
    // whereas volume keys are handled at the system level and always work.
    vTaskDelay(pdMS_TO_TICKS(40));
    uint8_t released[2] = {};
    return sendReport(kConsumerReportId, released, sizeof(released));
}

// ---- bond management (paired hosts) ----

int BleHid::maxBonds(void) { return kMaxBondSlots; }

int BleHid::bonds(BleBond *out, int maxCount)
{
    if (!out || maxCount <= 0 || !s_initialized.load()) return 0;

    ble_addr_t addrs[kMaxBondSlots] = {};
    const int wanted = maxCount < kMaxBondSlots ? maxCount : kMaxBondSlots;
    int found = 0;
    const int rc = ble_store_util_bonded_peers(addrs, &found, wanted);
    if (rc != 0) {
        ESP_LOGW(kTag, "Cannot enumerate bonds: rc=%d", rc);
        return 0;
    }

    // Identity address of the host we are talking to right now (if any), so the
    // list can mark that entry as connected.
    bool haveActive = false;
    ble_addr_t active = {};
    const uint16_t handle = s_connHandle.load();
    if (handle != kInvalidHandle) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(handle, &desc) == 0) {
            active = desc.peer_id_addr;
            haveActive = true;
        }
    }

    for (int i = 0; i < found; ++i) {
        out[i].addrType = addrs[i].type;
        std::memcpy(out[i].addr, addrs[i].val, sizeof(out[i].addr));
        out[i].connected = haveActive && active.type == addrs[i].type &&
                           std::memcmp(active.val, addrs[i].val, sizeof(active.val)) == 0;
    }
    return found;
}

esp_err_t BleHid::removeBond(const BleBond &bond)
{
    if (!s_initialized.load()) return ESP_ERR_INVALID_STATE;

    ble_addr_t addr = {};
    addr.type = bond.addrType;
    std::memcpy(addr.val, bond.addr, sizeof(addr.val));

    // ble_gap_unpair() terminates the connection, drops the keys and removes the
    // identity from the controller resolving list. Fall back to a plain store
    // delete if it reports the peer is not known to GAP.
    int rc = ble_gap_unpair(&addr);
    if (rc != 0) {
        ESP_LOGW(kTag, "ble_gap_unpair failed rc=%d; deleting store records", rc);
        rc = ble_store_util_delete_peer(&addr);
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot remove bond: rc=%d", rc);
        return ESP_FAIL;
    }
    s_bondStoreFull.store(false);
    return ESP_OK;
}

esp_err_t BleHid::removeAllBonds(void)
{
    if (!s_initialized.load()) return ESP_ERR_INVALID_STATE;

    esp_err_t firstErr = ESP_OK;
    BleBond list[kMaxBondSlots] = {};
    const int count = bonds(list, kMaxBondSlots);
    for (int i = 0; i < count; ++i) {
        const esp_err_t err = removeBond(list[i]);
        if (err != ESP_OK && firstErr == ESP_OK) firstErr = err;
    }
    s_bondStoreFull.store(false);
    return firstErr;
}

bool BleHid::bondStoreFull(void) { return s_bondStoreFull.load(); }
void BleHid::clearBondStoreFull(void) { s_bondStoreFull.store(false); }

void BleHid::formatAddr(const BleBond &bond, char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    // NimBLE keeps addresses least-significant byte first.
    std::snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X", bond.addr[5], bond.addr[4],
                  bond.addr[3], bond.addr[2], bond.addr[1], bond.addr[0]);
}

// ---- pairing confirmation ----

BlePairRequest BleHid::pairing(void)
{
    portENTER_CRITICAL(&s_pairLock);
    const auto request=s_pairRequest;
    portEXIT_CRITICAL(&s_pairLock);
    return request;
}

esp_err_t BleHid::confirmPairing(bool accept,uint32_t expectedGeneration)
{
    portENTER_CRITICAL(&s_pairLock);
    const auto request=s_pairRequest;
    const uint16_t handle=s_pairConnHandle;
    const bool valid=s_enabled.load() && request.action!=BlePairAction::None &&
        request.generation==expectedGeneration && handle!=kInvalidHandle && handle==s_connHandle.load();
    if(valid) {
        // Claim exactly this request before calling the host. A callback can
        // publish a newer request during injection; never clear that request.
        s_pairRequest.action=BlePairAction::None;
        s_pairRequest.passkey=0;
        s_pairConnHandle=kInvalidHandle;
    }
    portEXIT_CRITICAL(&s_pairLock);
    if(!valid) return ESP_ERR_INVALID_STATE;

    if(request.action==BlePairAction::Display) {
        // The passkey was already injected, the dialog is informational only.
        if(!accept) (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        return ESP_OK;
    }

    struct ble_sm_io io = {};
    io.action = BLE_SM_IOACT_NUMCMP;
    io.numcmp_accept = accept ? 1 : 0;
    const int rc = ble_sm_inject_io(handle, &io);
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot inject pairing answer: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void BleHid::setStateFromCallback(BleHidState state) { s_state.store(state); }
void BleHid::setConnectionHandle(uint16_t handle) { s_connHandle.store(handle); }
uint16_t BleHid::connectionHandle(void) { return s_connHandle.load(); }

void BleHid::startAdvertisingFromCallback(void)
{
    s_advPending.store(true); // callbacks never mutate advertisement fields
}
int BleHid::lastError() { const int radio=s_lastError.load(); return radio?radio:s_pairError.load(); }
void BleHid::poll()
{
    if(!s_initialized.load() || !s_enabled.load()) return;
    const auto now=esp_timer_get_time();
    if(!ble_hs_synced()) {
        const auto deadline=s_startDeadlineUs.load();
        if(deadline && now>=deadline) {
            if(s_state.exchange(BleHidState::Failed)!=BleHidState::Failed)
                ESP_LOGE(kTag,"NimBLE startup timed out host_running=%d",s_hostRunning.load());
            s_lastError.store(ESP_ERR_TIMEOUT);
        } else if(s_state.load()!=BleHidState::Failed) s_state.store(BleHidState::Starting);
        return;
    }
    s_startDeadlineUs.store(0);
    const auto handle=s_connHandle.load();
    if(handle!=kInvalidHandle) {
        const auto deadline=s_securityDeadlineUs.load();
        if(s_state.load()!=BleHidState::Connected && deadline && now>=deadline) {
            s_securityDeadlineUs.store(now+1000000);
            s_pairError.store(BLE_HS_ETIMEOUT); s_state.store(BleHidState::Failed);
            (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        }
        return;
    }
    if(now<s_nextAdvAttempt) return;
    // Permission is not a restart request. The controller stops advertising
    // before the CONNECT callback publishes our handle. Polling that gap must
    // not start a second connectable advertisement into the occupied slot.
    // Drain redundant requests while already active (including late HID_START).
    const bool restart=s_advRestart.exchange(false);
    const bool requested=s_advPending.exchange(false);
    if(!restart && !requested) return;
    if(restart && ble_gap_adv_active()) (void)ble_gap_adv_stop();
    if(!ble_gap_adv_active() && s_advertisingAllowed.load()) {
        s_nextAdvAttempt=now+1000000;
        processAdvertising();
    }
}
namespace {
void processAdvertising()
{
    if (!s_enabled.load() || !s_advertisingAllowed.load() ||
        s_connHandle.load()!=kInvalidHandle || !ble_hs_synced() || ble_gap_adv_active()) return;

    uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    int rc = ble_hs_id_infer_auto(0, &ownAddrType);
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot infer BLE address type: rc=%d", rc);
        s_lastError.store(rc); s_state.store(BleHidState::Failed);
        s_advPending.store(true); // explicit retry, rate-limited by poll()
        return;
    }
    s_ownAddrType.store(ownAddrType);

    // Connectable either way; only general discovery is time limited.
    const bool discoverable = s_discoverable.load();
    auto fields=s_advFields;
    fields.flags = static_cast<uint8_t>(BLE_HS_ADV_F_BREDR_UNSUP |
                                        (discoverable ? BLE_HS_ADV_F_DISC_GEN : 0));
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot set advertisement fields: rc=%d", rc);
        s_lastError.store(rc); s_state.store(BleHidState::Failed);
        s_advPending.store(true); // explicit retry, rate-limited by poll()
        return;
    }
    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = discoverable ? BLE_GAP_DISC_MODE_GEN : BLE_GAP_DISC_MODE_NON;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(discoverable?30:500);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(discoverable?50:1000);
    auto previousState=s_state.load();
    rc = ble_gap_adv_start(s_ownAddrType.load(), nullptr, BLE_HS_FOREVER,
                           &params, gapEvent, nullptr);
    // A real connection may win while the controller handles adv_start.
    // Never replace the GAP callback's Connected state with a spurious failure.
    if(s_connHandle.load()!=kInvalidHandle) return;
    const bool advertising=rc==0 || rc==BLE_HS_EALREADY;
    if (!advertising) {
        s_advPending.store(true); // a real failure retries; inactivity alone does not
        ESP_LOGE(kTag,"Cannot start advertising: rc=%d",rc);
    }
    else if (rc==0) ESP_LOGI(kTag, "Advertising as %s", kDeviceName);
    s_lastError.store(advertising?0:rc);
    // CONNECT/ENC_CHANGE callbacks own newer state even between the handle
    // check above and this publication. Never overwrite it from task context.
    s_state.compare_exchange_strong(previousState,advertising ? BleHidState::Advertising : BleHidState::Failed);
}

} // namespace
esp_err_t BleHid::prepareDeepSleep()
{
    (void)setEnabled(false);
    if(!s_initialized.load()) return ESP_OK;
    // Terminal path only; no deinit/free of GATT while callbacks may run.
    auto err=nimble_port_stop();
    if(err!=ESP_OK) return err;
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED)
        return esp_bt_controller_disable();
    return ESP_OK;
}
} // namespace sys