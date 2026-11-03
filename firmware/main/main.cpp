/*
 * New firmware entry point (three-layer architecture: core / ui / apps).
 *
 * Boot sequence:
 *   1. Power::init()      -> hold BAT_EN so the device stays on.
 *   2. OTA factory boot   -> always run from the factory partition.
 *   3. nvs_flash_init()   -> storage for Settings.
 *   4. bsp_display_start() -> display + touch + LVGL (backlight on, SD mount).
 *   5. Under the GUI lock: Settings::load, WindowManager::init, register apps,
 *      then create the home screen.
 *   6. A bounded GUI pump dispatches events; SystemService owns background I/O.
 *   7. Buttons::init()    -> physical keys -> EventBus.
 */
#include <ctime>
#include <cstdlib>
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/power_port.h"
#include "lvgl.h"

#include "core/event_bus.hpp"
#include "core/system_service.hpp"
#include "core/settings_store.hpp"
#include "core/clock_discipline.hpp"
#include "core/power_mgr.hpp"
#include "core/display_cast.hpp"
#include "core/screenshot.hpp"
#include "core/screen_orientation.hpp"
#include "core/button_mgr.hpp"
#include "core/battery_mgr.hpp"
#include "ui/window_mgr.hpp"
#include "ui/theme.hpp"
#include "ui/app_icons.hpp"
#include "ui/text_input.hpp"
#include "ui/quick_panel.hpp"
#include "ui/external_keyboard.hpp"
#include "ui/bluetooth_pairing.hpp"
#include "ui/boot_animation.hpp"
#include "ui/sysinfo_overlay.hpp"
#include "ui/screen_protection.hpp"
#include "apps/flashlight.hpp"
#include "apps/settings.hpp"
#include "apps/calendar.hpp"
#include "apps/dino.hpp"
#include "apps/gravity_maze.hpp"
#include "apps/morse.hpp"
#include "apps/bt_keyboard_mouse.hpp"
#include "apps/bt_music.hpp"
#include "apps/chiptune.hpp"
#include "apps/mp3_music.hpp"
#include "apps/watchface.hpp"
#include "apps/timer.hpp"
#include "apps/garden.hpp"
#include "apps/text_reader.hpp"
#include "apps/programming.hpp"
#include "apps/design_patterns.hpp"
#include "apps/photo_viewer.hpp"
#include "apps/file_manager.hpp"
#include "apps/password_vault.hpp"
#include "apps/recorder.hpp"
#include "apps/fc_player.hpp"
#include "apps/lan.hpp"
#include "apps/speed_test.hpp"
#include "apps/randomizer.hpp"
#include "apps/xiaozhi.hpp"
#include <memory>

#define TAG "main"

using sys::Buttons;
using sys::EventBus;
using sys::Event;
using sys::EventType;
using sys::Power;
using sys::Settings;
using ui::WindowManager;

// A short non-interactive notice lives above app roots, so navigation cannot
// leave a dangling timer. Remove it before another capture starts.
static lv_obj_t *screenshotNotice=nullptr;
static lv_timer_t *screenshotNoticeTimer=nullptr;
static void clear_screenshot_notice() {
    if(screenshotNoticeTimer) {lv_timer_delete(screenshotNoticeTimer);screenshotNoticeTimer=nullptr;}
    if(screenshotNotice) {lv_obj_delete(screenshotNotice);screenshotNotice=nullptr;}
}
static void screenshot_feedback(sys::Screenshot::Notice notice) {
    using Notice=sys::Screenshot::Notice;
    if(notice==Notice::None)return;
    clear_screenshot_notice();
    if(notice==Notice::Started || !Power::isScreenOn() || Power::sleepPending())return;
    const char *message=notice==Notice::Saved?"已保存 /screenshots":
        notice==Notice::Card?"保存失败：SD卡不可用":
        notice==Notice::Space?"保存失败：空间不足":
        notice==Notice::Memory?"保存失败：内存不足":"保存失败：请重试";
    screenshotNotice=lv_label_create(lv_layer_sys());
    lv_obj_remove_style_all(screenshotNotice);
    lv_obj_set_clickable(screenshotNotice,false);
    lv_obj_set_width(screenshotNotice,232);
    lv_obj_set_style_text_font(screenshotNotice,ui::theme::fontSmall(),0);
    lv_obj_set_style_text_color(screenshotNotice,lv_color_white(),0);
    lv_obj_set_style_bg_color(screenshotNotice,lv_color_black(),0);
    lv_obj_set_style_bg_opa(screenshotNotice,LV_OPA_90,0);
    lv_obj_set_style_pad_ver(screenshotNotice,6,0);
    lv_obj_set_style_text_align(screenshotNotice,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_text(screenshotNotice,message);
    lv_obj_align(screenshotNotice,LV_ALIGN_BOTTOM_MID,0,-8);
    screenshotNoticeTimer=lv_timer_create([](lv_timer_t *) {clear_screenshot_notice();},1800,nullptr);
}

// GUI-only pump: bounded dispatch and cached IMU reads. A settled orientation
// change sends only the panel-address commands under the existing LVGL lock.
static void ui_pump_cb(lv_timer_t *timer)
{
    sys::DisplayCast::poll();
    screenshot_feedback(sys::Screenshot::poll());
    Power::syncTouch();
    ui::BluetoothPairing::poll();
    ui::QuickPanel::poll();
    if(Power::sleepPending()) {
        ui::ScreenProtection::reset();
        // A fresh acknowledgement per request, even for rapid sleep/wake/sleep.
        if(!Power::sleepUiReady()) {
            clear_screenshot_notice();
            ui::BootAnimation::stop();
            WindowManager::instance().goHome(); // hides input, stops all app timers/SOS
            Power::syncTouch(); // cover PWR arriving after this pump's first sync
            Power::acknowledgeSleepUi();
        }
        return; // no app creation while preparing, suspended or restoring
    }
    ui::ExternalKeyboard::poll();
    EventBus::dispatch(8,ui::QuickPanel::allowEvent);
    WindowManager::instance().processNavigation();
    const bool wasSaving=Power::saving();
    Power::pollAutomatic(WindowManager::instance().powerHints());
    ui::ScreenProtection::poll(WindowManager::instance().allowsScreenProtection() &&
        !ui::QuickPanel::visible() && !ui::TextInput::visible() && !ui::BluetoothPairing::root() &&
        !ui::SysInfoOverlay::visible() && sys::Screenshot::idle() &&
        !sys::DisplayCast::enabled() && sys::DisplayCast::idle());
    if(wasSaving!=Power::saving())WindowManager::instance().powerSaving(Power::saving());
    sys::ScreenOrientation::poll(WindowManager::instance().locksScreenOrientation());
    lv_timer_set_period(timer,Power::slowIdle()?1000:20);
}

// Yumo（雨墨）is the project name; this note does not rename radio identities.
// Registered apps are added here as their modules are implemented.
static void register_apps(void)
{
    WindowManager::instance().registerApp(
        "密码本", LV_SYMBOL_EYE_CLOSE,
        [] { return std::make_unique<apps::PasswordVault>(); },"vault");
    WindowManager::instance().registerApp(
        "时钟", LV_SYMBOL_BELL,
        [] { return std::make_unique<apps::Watchface>(); },"clock");
    WindowManager::instance().registerApp("计时器", ui::icons::Timer,
        [] { return std::make_unique<apps::Timer>(); },"timer");
    WindowManager::instance().registerApp(
        "文本", LV_SYMBOL_FILE,
        [] { return std::make_unique<apps::TextReader>(); },"text");
    WindowManager::instance().registerApp(
        "蓝牙键鼠", LV_SYMBOL_KEYBOARD,
        [] { return std::make_unique<apps::BtKeyboardMouse>(); },"hid");
    WindowManager::instance().registerApp(
        "相册", LV_SYMBOL_IMAGE,
        [] { return std::make_unique<apps::PhotoViewer>(); },"photos");
    WindowManager::instance().registerApp("录音机", LV_SYMBOL_AUDIO,
        [] { return std::make_unique<apps::Recorder>(); },"recorder");
    WindowManager::instance().registerApp("随机", LV_SYMBOL_SHUFFLE,
        [] { return std::make_unique<apps::Randomizer>(); },"random");
    WindowManager::instance().registerApp("万年历", LV_SYMBOL_LIST,
        [] { return std::make_unique<apps::Calendar>(); },"calendar");
    WindowManager::instance().registerApp("编程速查", ui::icons::Programming,
        [] { return std::make_unique<apps::Programming>(); },"programming");
    WindowManager::instance().registerApp("设计模式", ui::icons::Patterns,
        [] { return std::make_unique<apps::DesignPatterns>(); },"patterns");
    WindowManager::instance().registerApp(
        "蓝牙音乐", LV_SYMBOL_BLUETOOTH,
        [] { return std::make_unique<apps::BtMusic>(); },"bt_music");
    WindowManager::instance().registerApp("MP3音乐", ui::icons::Music,
        [] { return std::make_unique<apps::Mp3Music>(); },"mp3");
    WindowManager::instance().registerApp("8Bit音乐", LV_SYMBOL_PLAY,
        [] { return std::make_unique<apps::Chiptune>(); },"chiptune");
    WindowManager::instance().registerApp(
        "手电筒", LV_SYMBOL_CHARGE,
        [] { return std::make_unique<apps::Flashlight>(); },"flashlight");
    WindowManager::instance().registerApp("摩斯电码", LV_SYMBOL_BULLET LV_SYMBOL_MINUS,
        [] { return std::make_unique<apps::Morse>(); },"morse");
    WindowManager::instance().registerApp("文件管理", LV_SYMBOL_DIRECTORY,
        [] { return std::make_unique<apps::FileManager>(); },"files");
    WindowManager::instance().registerApp("萌芽物语", ui::icons::Garden,
        [] { return std::make_unique<apps::Garden>(); },"garden");
    WindowManager::instance().registerApp("局域网", LV_SYMBOL_WIFI,
        [] { return std::make_unique<apps::Lan>(); },"lan");
    WindowManager::instance().registerApp("网速测速", LV_SYMBOL_UPLOAD,
        [] { return std::make_unique<apps::SpeedTest>(); },"speedtest");
    WindowManager::instance().registerApp("FC游戏", ui::icons::Fc,
        [] { return std::make_unique<apps::FcPlayer>(); },"fc");
    WindowManager::instance().registerApp("小恐龙", ui::icons::Dino,
        [] { return std::make_unique<apps::Dino>(); },"dino");
    WindowManager::instance().registerApp("重力迷宫", ui::icons::Maze,
        [] { return std::make_unique<apps::GravityMaze>(); },"maze");
    WindowManager::instance().registerApp("小智AI", ui::icons::Xiaozhi,
        [] { return std::make_unique<apps::Xiaozhi>(); },"xiaozhi");
    WindowManager::instance().registerApp(
        "设置", LV_SYMBOL_SETTINGS,
        [] { return std::make_unique<apps::Settings>(); },"settings");
}

extern "C" void app_main(void)
{
    Power::init();

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "[build] YUMO-INPUT-KEYS-20260921 running=%s offset=0x%lx",
             running ? running->label : "unknown",
             running ? static_cast<unsigned long>(running->address) : 0UL);

    // Keep subsequent boots pinned to the factory app partition.
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory != NULL) {
        ESP_LOGI(TAG, "Switch to partition factory");
        esp_ota_set_boot_partition(factory);
    }

    esp_err_t ret = nvs_flash_init();
    // Firmware updates must never silently discard BLE bonds/Wi-Fi settings.
    // Keep the UI usable on an NVS failure; radio init reports the error, and
    // recovery/erasure requires an explicit user decision.
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable (%s); preserving stored data", esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK(EventBus::init() ? ESP_OK : ESP_ERR_NO_MEM);
    setenv("TZ", "CST-8", 1);
    tzset();

    // Shared infrastructure only. The service starts both radios after UI unlock;
    // saved-target connection is a bounded, boot-only operation.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    lv_display_t *disp = bsp_display_start();
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_ERR_NO_MEM);
    lv_indev_t *indev = bsp_display_get_input_dev();
    if(!indev) ESP_LOGW(TAG, "Touch input unavailable");
    // Keep the backlight off until the first complete UI frame is ready.
    ESP_ERROR_CHECK(bsp_display_backlight_off());
    bsp_sdcard_mount();

    // Initialize the real battery ADC before the home panel starts polling it.
    if (!sys::Battery::init()) {
        ESP_LOGW(TAG, "Battery ADC unavailable; UI will show --%%");
    }

    // Load persisted settings before starting connectivity and building the UI.
    Settings::load();
    // Restore the last-known wall clock from NVS. This board has no battery-
    // backed RTC, so without this the time resets to zero on every reset or
    // brownout. SNTP will refine it later once WiFi connects.
    // RTC time survives deep sleep. Do not replace it with the pre-sleep NVS time.
    if(!Power::wokeFromSleep() || time(nullptr)<1609459200LL) Settings::restoreClock();
    sys::ClockDiscipline::boot();
    // Build UI while holding the GUI lock. Bring the display up before BLE:
    // a Bluetooth failure must never leave the device permanently black.
    // The adapter task is already running. Zero is a nonblocking attempt,
    // not an infinite wait: never mutate LVGL or unlock after a failed take.
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    ui::theme::init(static_cast<int>(Settings::theme()));
    // Register app factories before constructing HomeScreen. buildGrid() reads
    // this registry immediately; initializing the UI first creates an empty,
    // opaque black overlay which looks exactly like the display turned off.
    register_apps();
    WindowManager::instance().init();
    ui::TextInput::init(indev);
    ui::ScreenProtection::init(indev);
    ui::QuickPanel::init(indev);
    ui::ExternalKeyboard::init();
    sys::DisplayCast::init(disp);
    sys::Screenshot::init(disp);
    sys::ScreenOrientation::init(disp,indev);
    ui::BootAnimation::start();
    lv_refr_now(nullptr);
    // The BSP brightness setter enables PWM, so apply it only after the first
    // complete frame has been submitted to the panel.
    Power::applyBrightness(Settings::brightness());
    bsp_power_set_pump(lv_timer_create(ui_pump_cb,20,nullptr),Power::activity);
    ui::SysInfoOverlay::init(disp);
    ui::SysInfoOverlay::setVisible(Settings::sysInfoOn());
    bsp_display_unlock();

    // All runtime radio, codec and NVS work belongs to one worker.
    if (!sys::SystemService::start()) {
        ESP_LOGE(TAG, "System service unavailable; UI remains usable");
    }

    // Physical buttons (BOOT/PLUS/PWR) -> EventBus.
    Buttons::init();

}
