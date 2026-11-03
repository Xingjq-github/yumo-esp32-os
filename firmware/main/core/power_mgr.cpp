#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "password_vault.hpp"
#include "power_mgr.hpp"
#include "game_audio.hpp"
#include "usb_storage.hpp"
#include "deep_sleep_wake.h"
#include "display_cast.hpp"
#include "screenshot.hpp"
#include "lan_hotspot.hpp"
#include "speed_test.hpp"
#include "event_bus.hpp"
#include "settings_store.hpp"
#include "clock_discipline.hpp"
#include "system_service.hpp"
#include "audio_mgr.hpp"
#include "battery_mgr.hpp"
#include "audio_recorder.hpp"
#include "fc_emulator.hpp"
#include "qmi8658.hpp"
#include "ble_hid.hpp"
#include "storage_browser.hpp"
#include "sd_media.hpp"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/power_port.h"
#include "lvgl.h"
#include <atomic>
#include <algorithm>
#include <ctime>
#include <mutex>
namespace sys {
namespace {
constexpr gpio_num_t kBatEnPin=GPIO_NUM_2,kPwrPin=GPIO_NUM_5;
std::atomic<bool> screen{true},shuttingDown{false},sleepRequested{false},uiStopped{false};
std::atomic<bool> automaticDark{false};
std::atomic<bool> automaticSleepRequested{false};
std::atomic<int64_t> sleepRequestedAt{0};
std::mutex displayMutex;
TaskHandle_t sleepTask=nullptr;
std::atomic<bool> wakeRequested{false},standbyActive{false};
std::atomic<bool> terminalPhase{false},sleepReady{false};
std::atomic<bool> themeRestartReady{false},themeRestartFailed{false};
std::atomic<bool> shutdownFailed{false};
bool shutdownRequested=false; // SystemService worker only
// Written/read only by the service worker, before publishing sleepRequested.
int restartTheme=0;
// Worker-only peripheral state. PM bookkeeping is separately serialized because
// both GUI/PWR and workers use it. Never take displayMutex while holding pmMutex.
bool displayNeedsResume=false;
std::mutex pmMutex;
esp_pm_lock_handle_t performanceLock=nullptr, workloadLock=nullptr;
esp_pm_lock_handle_t sleepGate=nullptr;
bool sleepGateHeld=true,economyDim=false;
bool performanceHeld=false, dfsReady=false;
bool holdPerformanceLocked(bool hold) {
    if(!performanceLock) return false;
    if(!hold && !dfsReady) return true; // failed setup: keep the normal CPU speed
    if(performanceHeld==hold) return true;
    const auto err=hold?esp_pm_lock_acquire(performanceLock):esp_pm_lock_release(performanceLock);
    if(err==ESP_OK) performanceHeld=hold;
    else ESP_LOGW("power","CPU frequency lock: %s",esp_err_to_name(err));
    return err==ESP_OK;
}
void holdPerformance(bool hold) {
    const std::lock_guard<std::mutex> lock(pmMutex);
    (void)holdPerformanceLocked(hold);
}
int temporaryBrightness=-1;
bool castDisplay=false; // guarded by displayMutex; independent of logical screen
portMUX_TYPE latchMux=portMUX_INITIALIZER_UNLOCKED;
// Settings deep sleep and USB shutdown fallback share the internal-stack executor.
// IDF rejects entry from a PSRAM stack; the service worker must never enter it.
void sleepEntry(void *) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        // Theme restart has no forced timeout and reaches here only after every
        // I/O fence and both NVS commits succeeded. Run on this INTERNAL stack.
        if(themeRestartReady.load() && !shuttingDown.load()) esp_restart();
        if(!terminalPhase.load() && !shuttingDown.load()) continue;
        if(!sleepReady.load() && !shuttingDown.load()) {
            ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(8000));
            if(!sleepReady.load() && !shuttingDown.load())
                ESP_LOGW("power","radio shutdown timeout; entering hardware deep sleep");
        }
        while(gpio_get_level(kPwrPin)==0) vTaskDelay(pdMS_TO_TICKS(20));
        // Never raise BAT_EN: an emergency powerOff has already dropped it.
        portENTER_CRITICAL(&latchMux); gpio_hold_en(kBatEnPin); portEXIT_CRITICAL(&latchMux);
        gpio_set_direction(BSP_LCD_BL,GPIO_MODE_OUTPUT); gpio_set_level(BSP_LCD_BL,0);
        gpio_set_direction(BSP_POWER_AMP_IO,GPIO_MODE_OUTPUT); gpio_set_level(BSP_POWER_AMP_IO,0);
        gpio_hold_en(BSP_LCD_BL); gpio_hold_en(BSP_POWER_AMP_IO);
        gpio_deep_sleep_hold_en();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_ext0_wakeup(kPwrPin,0);
        rtc_gpio_pullup_en(kPwrPin); rtc_gpio_pulldown_dis(kPwrPin);
        deep_sleep_arm_double_click();
        // EXT0 is the only wake source. RTC/system time keeps advancing while
        // CPUs, radios and flash are off; no periodic wake/GUI/service work.
        ESP_LOGI("power","deep sleep: RTC retained; double-click PWR to boot");
        ClockDiscipline::prepareSleep();
        esp_deep_sleep_start();
    }
}
void setScreenLocked(bool on) {
    if(!on) DisplayCast::cancel();
    // Revoke queued private HID strokes before GUI ScreenOff dispatch.
    if(!on) BleHid::cancelPrivateInput();
    if(!on) BleHid::cancelKeys();
    if(!on) BleHid::cancelMouse();

    if(shuttingDown.load() || screen.load()==on) return;
    if(on && sleepRequested.load()) return;
    if(on) holdPerformance(true); // full speed BEFORE waking/repainting the panel
    screen.store(on);
    if(on && !castDisplay) bsp_display_brightness_set(temporaryBrightness>=0?temporaryBrightness:Settings::brightness());
    else if(on) bsp_display_backlight_off();
    else { temporaryBrightness=-1; bsp_display_backlight_off(); }
    if(shuttingDown.load()) { screen.store(false); bsp_display_backlight_off(); }
    if(!on) holdPerformance(false); // DFS between active background work; no radio changes
    EventBus::publish({on?EventType::ScreenOn:EventType::ScreenOff});
}
void requestRecovery(const char *reason) {
    ESP_LOGW("power","standby cancelled: %s; preserving wireless links",reason);
    wakeRequested.store(true); // worker restores any partially suspended peripherals
}
}
void Power::init() {
    // Re-establish the battery latch BEFORE releasing deep-sleep GPIO holds.
    gpio_config_t io{}; io.mode=GPIO_MODE_OUTPUT;
    io.pin_bit_mask=(1ULL<<kBatEnPin)|(1ULL<<BSP_POWER_AMP_IO);
    gpio_config(&io); gpio_set_level(kBatEnPin,1);
    gpio_set_direction(BSP_LCD_BL,GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_BL,0); gpio_set_level(BSP_POWER_AMP_IO,0);
    gpio_hold_dis(kBatEnPin);
    gpio_deep_sleep_hold_dis(); gpio_hold_dis(BSP_LCD_BL); gpio_hold_dis(BSP_POWER_AMP_IO);
    rtc_gpio_deinit(kPwrPin); // ext0 leaves this pad in RTC mode after wake
    gpio_config_t key{}; key.pin_bit_mask=1ULL<<kPwrPin; key.mode=GPIO_MODE_INPUT;
    key.pull_up_en=GPIO_PULLUP_ENABLE; gpio_config(&key);
    if(xTaskCreateWithCaps(sleepEntry,"sleep_entry",4096,nullptr,12,&sleepTask,
                          MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS) {
        sleepTask=nullptr; ESP_LOGE("power","Internal-stack shutdown executor unavailable");
    }
    // Gate automatic light sleep until GPIO input and idle policy are ready.
    // The panel retains its RAM; RC_FAST PWM keeps static lit pages visible.
    const std::lock_guard<std::mutex> lock(pmMutex);
    auto err=esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"display_active",&performanceLock);
    if(err==ESP_OK && !holdPerformanceLocked(true)) err=ESP_FAIL;
    if(err==ESP_OK) err=esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"background_work",&workloadLock);
    if(err==ESP_OK) err=esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"interactive",&sleepGate);
    if(err==ESP_OK) err=esp_pm_lock_acquire(sleepGate);
    if(err==ESP_OK) {
        esp_pm_config_t config{};
        config.max_freq_mhz=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        config.min_freq_mhz=40;
        config.light_sleep_enable=true;
        err=esp_pm_configure(&config);
        dfsReady=err==ESP_OK;
    }
    if(err!=ESP_OK) ESP_LOGW("power","DFS unavailable: %s; retaining normal performance",esp_err_to_name(err));
}
Power::WorkGuard::WorkGuard() {
    const std::lock_guard<std::mutex> lock(pmMutex);
    if(!dfsReady) return;
    const auto err=esp_pm_lock_acquire(workloadLock);
    acquired=err==ESP_OK;
    if(!acquired) {
        dfsReady=false; (void)holdPerformanceLocked(true);
        ESP_LOGW("power","Work lock failed: %s; retaining display performance lock",esp_err_to_name(err));
    }
}
Power::WorkGuard::~WorkGuard() {
    if(!acquired) return;
    const std::lock_guard<std::mutex> lock(pmMutex);
    const auto err=esp_pm_lock_release(workloadLock);
    if(err!=ESP_OK) ESP_LOGW("power","Work lock release: %s",esp_err_to_name(err));
}
void Power::screenOn() { activity();const std::lock_guard<std::mutex> lock(displayMutex); automaticDark=false;setScreenLocked(true); }
void Power::screenOff() { activity();const std::lock_guard<std::mutex> lock(displayMutex); automaticDark=false;setScreenLocked(false); }
void Power::automaticScreenOff() {
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(!automaticScreenOffAllowed() || sleepPending())return;
    automaticDark=true;setScreenLocked(false);
}
bool Power::audioMayRun() { return (screen.load() || automaticDark.load()) && !sleepPending(); }
void Power::restoreAutomaticScreen() {
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(!automaticDark.load() || sleepPending())return;
    automaticDark=false;setScreenLocked(true);
}
void Power::toggleScreen() {
    activity();automaticDark=false;
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(sleepRequested.load()) { if(!terminalPhase.load()) wakeRequested.store(true); return; }
    setScreenLocked(!screen.load());
}
bool Power::isScreenOn() { return screen.load(); }
void Power::setEconomy(bool low,bool dim,bool allowLightSleep) {
    const std::lock_guard<std::mutex> displayLock(displayMutex);
    const bool supplyBlocked=!automaticSleepAllowed();
    if(shuttingDown.load() || sleepPending() || supplyBlocked) {low=false;dim=false;allowLightSleep=false;}
    if(economyDim!=dim) {
        economyDim=dim;
        if(screen.load() && !castDisplay && temporaryBrightness<0)
            bsp_display_brightness_set(dim?std::min(Settings::brightness(),int(Settings::powerConfig().brightness)):Settings::brightness());
    }
    const std::lock_guard<std::mutex> lock(pmMutex);
    if(!dfsReady)return;
    holdPerformanceLocked((screen.load() && !low) || supplyBlocked);
    const bool hold=!allowLightSleep;
    if(sleepGate && sleepGateHeld!=hold) {
        const auto err=hold?esp_pm_lock_acquire(sleepGate):esp_pm_lock_release(sleepGate);
        if(err==ESP_OK)sleepGateHeld=hold;
    }
}
void Power::setDisplayCast(bool active) {
    const std::lock_guard<std::mutex> lock(displayMutex);
    active=active && screen.load() && !shuttingDown.load() && !sleepRequested.load();
    if(castDisplay==active)return;
    castDisplay=active;
    if(active || !screen.load() || shuttingDown.load())bsp_display_backlight_off();
    else {
        const int normal=Settings::brightness();
        const int restored=temporaryBrightness>=0?temporaryBrightness:
            economyDim?std::min(normal,int(Settings::powerConfig().brightness)):normal;
        bsp_display_brightness_set(restored);
    }
    if(shuttingDown.load())bsp_display_backlight_off();
}
void Power::applyBrightness(int percent) {
    percent=std::max(5,std::min(100,percent));
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(shuttingDown.load()) return;
    if(screen.load() && !castDisplay && temporaryBrightness<0) bsp_display_brightness_set(percent);
    if(shuttingDown.load()) { screen.store(false); bsp_display_backlight_off(); }
    EventBus::publish({EventType::BrightnessChanged,percent});
}
void Power::setTransientBrightness(int percent) {
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(shuttingDown.load() || sleepRequested.load() || !screen.load()) return;
    percent=std::max(0,std::min(100,percent));
    if(temporaryBrightness==percent) return;
    temporaryBrightness=percent; if(!castDisplay) bsp_display_brightness_set(percent);
    if(shuttingDown.load()) { screen.store(false); bsp_display_backlight_off(); }
}
void Power::clearTransientBrightness() {
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(temporaryBrightness<0) return;
    temporaryBrightness=-1;
    if(!shuttingDown.load() && screen.load() && !castDisplay) bsp_display_brightness_set(Settings::brightness());
    if(shuttingDown.load()) { screen.store(false); bsp_display_backlight_off(); }
}
esp_err_t Power::deepSleep(bool automatic) {
    if(!automatic && !UsbStorage::idle())return ESP_ERR_INVALID_STATE;
    if(automatic && !automaticDeepSleepAllowed())return ESP_ERR_INVALID_STATE;
    if(!sleepTask || !SystemService::running() || shuttingDown.load() || sleepRequested.load()) return ESP_ERR_INVALID_STATE;
    standby(automatic);
    return ESP_OK;
}
esp_err_t Power::restartForTheme(themes::Id id) {
    if(!UsbStorage::idle())return ESP_ERR_INVALID_STATE;
    if(!themes::valid(static_cast<int>(id))) return ESP_ERR_INVALID_ARG;
    if(!sleepTask || !SystemService::running() || shuttingDown.load() || sleepRequested.load()) return ESP_ERR_INVALID_STATE;
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(shuttingDown.load() || sleepRequested.load()) return ESP_ERR_INVALID_STATE;
    restartTheme=static_cast<int>(id);
    automaticSleepRequested=false;
    themeRestartFailed.store(false); themeRestartReady.store(false);
    sleepRequestedAt.store(esp_timer_get_time()); uiStopped.store(false);
    wakeRequested.store(false); sleepRequested.store(true);
    setScreenLocked(false);
    SystemService::cancelBluetoothScan(); SystemService::cancelWifiScan();
    return ESP_OK;
}
bool Power::takeThemeRestartError() { return themeRestartFailed.exchange(false); }
bool Power::takeShutdownError() { return shutdownFailed.exchange(false); }
esp_err_t Power::shutdown() {
    if(!UsbStorage::idle() || !sleepTask || !SystemService::running() || shuttingDown.load() || sleepRequested.load())
        return ESP_ERR_INVALID_STATE;
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(shuttingDown.load() || sleepRequested.load()) return ESP_ERR_INVALID_STATE;
    shutdownRequested=true; shutdownFailed.store(false); automaticSleepRequested=false;
    sleepRequestedAt.store(esp_timer_get_time()); uiStopped.store(false);
    wakeRequested.store(false); sleepRequested.store(true);
    setScreenLocked(false);
    SystemService::cancelBluetoothScan(); SystemService::cancelWifiScan();
    return ESP_OK;
}
void Power::standby(bool automatic) {
    if(!automatic && !UsbStorage::idle())return;
    if(!SystemService::running() || shuttingDown.load()) return;
    const std::lock_guard<std::mutex> lock(displayMutex);
    if(shuttingDown.load()) return;
    if(automatic && (isScreenOn() || !automaticDeepSleepAllowed()))return;
    if(sleepRequested.load()) {
        wakeRequested.store(true); // also cancels an in-flight entry; no lost wake click
        ESP_LOGI("power","PWR wake requested");
        return;
    }
    sleepRequestedAt.store(esp_timer_get_time()); uiStopped.store(false);
    automaticSleepRequested=automatic;
    wakeRequested.store(false);
    sleepRequested.store(true); // publish only after acknowledgement state is reset
    setScreenLocked(false); // SOS cannot illuminate the panel during preparation
    if(automatic) (void)UsbStorage::setEnabled(false); // fence new host I/O, drain accepted writes
    SystemService::cancelBluetoothScan();
    ESP_LOGI("power","Settings deep sleep requested; save radio preferences, retain RTC");
}
bool Power::sleepPending() { return sleepRequested.load(); }
bool Power::inStandby() { return standbyActive.load(); }
bool Power::sleepTerminal() { return terminalPhase.load(); }
bool Power::sleepUiReady() { return uiStopped.load(); }
void Power::acknowledgeSleepUi() { uiStopped.store(true); }
bool Power::wokeFromSleep() { return esp_reset_reason()==ESP_RST_DEEPSLEEP; }
void Power::processStandby() {
    if(!sleepRequested.load() || shuttingDown.load()) return;
    if(terminalPhase.load()) return;
    // Refresh hardware in this sole worker immediately before the destructive
    // phase, not just when the GUI originally queued automatic deep sleep.
    if(automaticSleepRequested.load()) {
        Battery::poll(isScreenOn());
        if(!automaticDeepSleepAllowed())wakeRequested=true;
    }
    if(wakeRequested.exchange(false)) {
        if(restartTheme) { restartTheme=0; themeRestartFailed.store(true); }
        if(shutdownRequested) { shutdownRequested=false; shutdownFailed.store(true); }
        holdPerformance(true); // restore full CPU speed BEFORE display/touch recovery
        if(displayNeedsResume) {
            const auto err=bsp_display_resume();
            if(err!=ESP_OK) {
                // Fail closed, never reboot/disconnect BLE on a display fault.
                // A subsequent PWR click retries; the service and watchdog stay alive.
                standbyActive.store(true); holdPerformance(false);
                ESP_LOGE("power","Display wake failed: %s; press PWR to retry",esp_err_to_name(err));
                return;
            }
            displayNeedsResume=false;
        }
        if(shuttingDown.load()) return;
        if(!Qmi8658::resumeFromStandby()) ESP_LOGW("power","IMU wake failed");
        if(!Audio::resumeFromStandby()) ESP_LOGW("power","Audio wake failed");
        {
            const std::lock_guard<std::mutex> lock(displayMutex);
            if(shuttingDown.load()) return;
            standbyActive.store(false); uiStopped.store(false);
            sleepRequested.store(false);
            automaticSleepRequested=false;
            setScreenLocked(true);
        }
        ESP_LOGI("power","Deep sleep cancelled before radio shutdown; BLE connected=%d",BleHid::connected());
        return;
    }
    if(standbyActive.load()) return;
    // Unlike deep sleep, never force a theme restart when an I/O fence stalls.
    if((restartTheme || shutdownRequested) && esp_timer_get_time()-sleepRequestedAt.load()>10000000) {
        requestRecovery("shutdown/restart drain timeout"); return;
    }
    if(!uiStopped.load()) {
        if(esp_timer_get_time()-sleepRequestedAt.load()>2000000) requestRecovery("GUI acknowledgement timeout");
        return;
    }
    if(gpio_get_level(kPwrPin)==0) return;
    if(!UsbStorage::idle())return; // worker flushes media before releasing USB ownership
    if(!DisplayCast::idle())return; // USB ISR/buffers must be released before sleep.
    if(!Screenshot::idle())return; // Finish/close the PNG before SD unmount/restart.
    // Give an in-flight keyboard chord time to release before stopping NimBLE.
    if(!BleHid::keysIdle()) {
        if(esp_timer_get_time()-sleepRequestedAt.load()>2000000)requestRecovery("keyboard release busy");
        return;
    }
    // The mouse queue preserves all-up across transient controller pressure.
    // Allow its bounded release recovery before attempting terminal shutdown.
    if(!BleHid::mouseIdle()) {
        if(esp_timer_get_time()-sleepRequestedAt.load()>6000000)requestRecovery("mouse release busy");
        return;
    }
    // Do not enter deep sleep while a foreground hotspot is still releasing
    // its AP_STOP fence, DHCP interface or radio resources.
    if(!LanHotspot::idle()) {
        if(esp_timer_get_time()-sleepRequestedAt.load()>10000000) requestRecovery("hotspot shutdown busy");
        return;
    }
    if(!core::speed::idle()) {
        if(esp_timer_get_time()-sleepRequestedAt.load()>10000000)requestRecovery("speed test shutdown busy");
        return;
    }
    // UI has cancelled its workers. Do not unmount a card still being read.
    if(core::sd::busy()) return; // bounded eject/read finishes before SD teardown
    if(!core::xiaozhi::idle() || !core::mp3::idle() || !core::chip::idle() || !core::game_audio::idle() || !core::storageIdle() || !vault::idle() || !core::recorder::idle() || !core::fc::idle()) {
        if(!core::xiaozhi::idle() || !core::mp3::idle() || !core::chip::idle() || !core::game_audio::idle() || !vault::idle() || !core::recorder::idle() || !core::fc::idle()) {
            if(esp_timer_get_time()-sleepRequestedAt.load()>10000000) requestRecovery("app storage worker busy");
        } else if(esp_timer_get_time()-sleepRequestedAt.load()>2000000) requestRecovery("SD reader busy");
        return;
    }
    Settings::saveClock(time(nullptr));
    if(!Settings::flush(true)) { requestRecovery("settings save failed"); return; }
    if(shutdownRequested) {
        if(wakeRequested.load()) return;
        const auto card=core::sd::prepareRestart();
        if(card==core::sd::RestartState::Busy) return;
        if(card==core::sd::RestartState::Failed) { requestRecovery("shutdown SD unmount failed"); return; }
        const std::lock_guard<std::mutex> lock(displayMutex);
        if(wakeRequested.load() || shuttingDown.load()) return;
        shutdownRequested=false;
        terminalPhase.store(true);
        powerOff(); // Emergency primitive is safe now: all writers have drained.
        return;
    }
    if(restartTheme) {
        if(wakeRequested.load()) return;
        const auto card=core::sd::prepareRestart();
        if(card==core::sd::RestartState::Busy) return;
        if(card==core::sd::RestartState::Failed) {
            requestRecovery("theme restart SD unmount failed"); return;
        }
        // PWR may cancel until the commit begins. Serialize it with displayMutex
        // so a late click cannot report cancellation after saving a new theme.
        const std::lock_guard<std::mutex> lock(displayMutex);
        if(wakeRequested.load() || shuttingDown.load()) return;
        if(!Settings::saveTheme(static_cast<themes::Id>(restartTheme))) {
            requestRecovery("theme save failed"); return;
        }
        terminalPhase.store(true); themeRestartReady.store(true);
        xTaskNotifyGive(sleepTask);
        return;
    }
    // A failed suspend may already have sent SLPIN: always run its paired resume.
    displayNeedsResume=true;
    auto err=bsp_display_suspend();
    if(err!=ESP_OK) { requestRecovery("display/touch suspend failed"); return; }
    if(shuttingDown.load()) return;
    if(!Qmi8658::suspendForStandby()) ESP_LOGW("power","IMU standby failed");
    Audio::suspendForStandby();
    if(automaticSleepRequested.load()) {
        Battery::poll(isScreenOn());
        if(!automaticDeepSleepAllowed()) {requestRecovery("automatic sleep settings protection");return;}
    }
    // Serialize the final commit with PWR cancellation, as for theme restart.
    // Never accept a wake request between its last check and terminal entry.
    {
        const std::lock_guard<std::mutex> lock(displayMutex);
        if(wakeRequested.load()) return;
        if(shuttingDown.load()) return;
        standbyActive.store(true); terminalPhase.store(true);
    }
    holdPerformance(false);
    // Save credentials/settings BEFORE stopping hardware. Teardown does not
    // rewrite user data; runtime radio permissions still default off after reboot.
    xTaskNotifyGive(sleepTask); // bounded 8s fallback even if a peripheral stop blocks
    if(bsp_sdcard_get_handle()) {
        const auto sdError=bsp_sdcard_unmount(); // no open App/storage-worker handles; stop SD clocks
        if(sdError!=ESP_OK) ESP_LOGW("power","SD shutdown: %s",esp_err_to_name(sdError));
    }
    (void)esp_wifi_stop();
    const auto bleError=BleHid::prepareDeepSleep();
    if(bleError!=ESP_OK) ESP_LOGW("power","BLE shutdown: %s",esp_err_to_name(bleError));
    sleepReady.store(true); xTaskNotifyGive(sleepTask);
}
void Power::syncTouch() {
    static bool applied=true;
    const bool desired=screen.load();
    if(applied==desired) return;
    auto indev=bsp_display_get_input_dev(); if(!indev) return;
    // Dark-screen touch is disabled in LVGL, so its level-triggered GPIO must
    // not repeatedly wake light sleep without anyone consuming the packet.
    // PWR/BOOT/KEY wake sources are independent and remain armed.
    const auto wakeError=desired?gpio_wakeup_enable(BSP_LCD_TOUCH_INT,GPIO_INTR_LOW_LEVEL):
                                 gpio_wakeup_disable(BSP_LCD_TOUCH_INT);
    if(wakeError!=ESP_OK) ESP_LOGW("power","Touch wake gate: %s",esp_err_to_name(wakeError));
    bsp_power_touch_active(desired);
    lv_indev_reset(indev,nullptr);
    lv_indev_enable(indev,desired); applied=desired;
    // LCD RAM is not assumed to survive SLPIN; repaint without recreating apps.
    if(desired) lv_obj_invalidate(lv_screen_active());
}
void Power::powerOff() {
    DisplayCast::cancel();
    // Independent high-priority escape, even if GUI or standby recovery is stuck.
    portENTER_CRITICAL(&latchMux);
    gpio_hold_dis(kBatEnPin); gpio_set_level(kBatEnPin,0); gpio_hold_en(kBatEnPin);
    portEXIT_CRITICAL(&latchMux);
    if(shuttingDown.exchange(true)) return;
    sleepRequested.store(true);
    screen.store(false); bsp_display_backlight_off();
    if(sleepTask) xTaskNotifyGive(sleepTask);
    else ESP_LOGE("power","Battery latch is off; USB sleep unavailable (no internal task)");
}
} // namespace sys
