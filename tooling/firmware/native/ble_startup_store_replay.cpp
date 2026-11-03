// Replays the unchanged production BleHid::init body with platform stubs.
// Tests the storage gate, publication and cleanup; it does not emulate BLE RF.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <initializer_list>
#include "core/ble_hid.hpp"

constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 257;
constexpr int ESP_ERR_INVALID_STATE = 259, ESP_ERR_INVALID_CRC = 265;
constexpr int BLE_HS_ENOMEM = 6, MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
constexpr int MALLOC_CAP_INTERNAL = 4, ESP_HID_TRANSPORT_BLE = 1;
constexpr int ESP_HID_APPEARANCE_KEYBOARD = 961;
constexpr int ESP_LOG_WARN = 2;
constexpr int CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE = 4096;
constexpr int CONFIG_BT_NIMBLE_PINNED_TO_CORE = 0, configMAX_PRIORITIES = 25;
#define ESP_LOGE(...)
using StackType_t = uint32_t;
struct esp_hidd_dev_t {};
struct nvs_stats_t {};
unsigned checks = 0, scenarios = 0;
#define CHECK(value) do { ++checks; if (!(value)) { \
    std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); std::abort(); } } while (0)

namespace fake {
esp_err_t nvsError = ESP_OK, storeError = ESP_OK;
unsigned nvsReads = 0, storeInitializations = 0, statusReads = 0, hostStarts = 0;
unsigned nimbleStarts = 0, hidStarts = 0, nimbleStops = 0, hidStops = 0;
esp_hidd_dev_t device;
}
esp_err_t nvs_get_stats(const char*, nvs_stats_t*) { ++fake::nvsReads; return fake::nvsError; }
void* heap_caps_malloc(size_t bytes, unsigned) { return std::malloc(bytes); }
void esp_log_level_set(const char*, int) {}
void heap_caps_free(void* memory) { std::free(memory); }
esp_err_t nimble_port_init() { ++fake::nimbleStarts; return ESP_OK; }
esp_err_t nimble_port_deinit() { ++fake::nimbleStops; return ESP_OK; }
esp_err_t esp_hidd_dev_init(const int*, int, void (*)(), esp_hidd_dev_t** out) {
    ++fake::hidStarts; *out = &fake::device; return ESP_OK;
}
esp_err_t esp_hidd_dev_deinit(esp_hidd_dev_t* device) {
    CHECK(device == &fake::device); ++fake::hidStops; return ESP_OK;
}
int ble_svc_gap_device_name_set(const char*) { return 0; }
int ble_svc_gap_device_appearance_set(int) { return 0; }
void ble_store_config_init() { ++fake::storeInitializations; }
esp_err_t yumo_ble_store_status() {
    CHECK(fake::storeInitializations > fake::statusReads);
    ++fake::statusReads; return fake::storeError;
}
int64_t esp_timer_get_time() { return 1234567; }
void* xTaskCreateStaticPinnedToCore(void (*)(void*), const char*, unsigned, void*,
                                 unsigned, StackType_t*, int*, unsigned) {
    CHECK(fake::storeError == ESP_OK);
    CHECK(fake::statusReads != 0);
    ++fake::hostStarts; return &fake::device;
}
struct { void (*store_status_cb)() = nullptr; } ble_hs_cfg;
namespace sys {
std::recursive_mutex s_scanControlMutex;
std::atomic<bool> s_initialized{false}, s_enabled{false};
std::atomic<BleHidState> s_state{BleHidState::Disabled};
std::atomic<esp_hidd_dev_t*> s_device{nullptr};
std::atomic<int64_t> s_startDeadlineUs{0};
std::atomic<esp_err_t> s_lastError{0};
BleScanDevice* s_scanDevices = nullptr;
StackType_t* s_hostStack = nullptr;
int s_hostTaskControl = 0, s_deviceConfig = 0;
constexpr unsigned kHostStackCaps = 6;
constexpr int64_t kHostStartTimeoutUs = 6000000;
constexpr char kDeviceName[] = "Yumo synthetic";
struct KeyboardInput { static bool init() { return true; } };
struct BlePeerStore { static void load() {} };
void configureGap() {}
void hiddEvent() {}
void bondStoreStatus() {}
void hostTask(void*) {}
#include "ble_startup.inc"
}
void reset() {
    ++scenarios;
    std::free(sys::s_scanDevices); sys::s_scanDevices = nullptr;
    std::free(sys::s_hostStack); sys::s_hostStack = nullptr;
    fake::nvsError = fake::storeError = ESP_OK;
    fake::nvsReads = fake::storeInitializations = fake::statusReads = fake::hostStarts = 0;
    fake::nimbleStarts = fake::hidStarts = fake::nimbleStops = fake::hidStops = 0;
    sys::s_initialized = false; sys::s_enabled = true;
    sys::s_state = sys::BleHidState::Disabled;
    sys::s_device = nullptr; sys::s_startDeadlineUs = 0; sys::s_lastError = 0;
    ble_hs_cfg.store_status_cb = nullptr;
}
void assertFailed(esp_err_t error) {
    CHECK(!sys::s_initialized.load()); CHECK(!sys::s_enabled.load());
    CHECK(sys::s_device.load() == nullptr); CHECK(sys::s_hostStack == nullptr);
    CHECK(sys::s_state.load() == sys::BleHidState::Failed);
    CHECK(sys::s_startDeadlineUs.load() == 0); CHECK(sys::s_lastError.load() == error);
    CHECK(fake::hostStarts == 0); CHECK(ble_hs_cfg.store_status_cb == nullptr);
}
int main() {
    reset();
    CHECK(sys::BleHid::init() == ESP_OK);
    CHECK(fake::hostStarts == 1 && fake::statusReads == 1);
    CHECK(sys::s_initialized.load()); CHECK(sys::s_device.load() == &fake::device);
    CHECK(sys::s_startDeadlineUs.load() > esp_timer_get_time());
    CHECK(ble_hs_cfg.store_status_cb == sys::bondStoreStatus);
    CHECK(sys::BleHid::init() == ESP_OK);
    CHECK(fake::nvsReads == 1 && fake::storeInitializations == 1 && fake::hostStarts == 1);

    for (const int error : {ESP_ERR_INVALID_CRC, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE}) {
        reset(); fake::storeError = error;
        CHECK(sys::BleHid::init() == error);
        assertFailed(error);
        CHECK(fake::hidStarts == 1 && fake::nimbleStarts == 1);
        CHECK(fake::hidStops == 1 && fake::nimbleStops == 1);
        CHECK(fake::statusReads == 1);
        // A later initialization actually retries the storage initialization.
        fake::storeError = ESP_OK;
        CHECK(sys::BleHid::init() == ESP_OK);
        CHECK(fake::hostStarts == 1 && fake::statusReads == 2);
        CHECK(sys::s_initialized.load());
    }
    reset(); fake::nvsError = ESP_ERR_INVALID_STATE;
    CHECK(sys::BleHid::init() == ESP_ERR_INVALID_STATE);
    assertFailed(ESP_ERR_INVALID_STATE);
    CHECK(fake::nimbleStarts == 0 && fake::storeInitializations == 0);
    std::free(sys::s_scanDevices); std::free(sys::s_hostStack);
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u}\n", scenarios, checks);
}
