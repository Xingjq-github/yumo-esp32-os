#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "power_mgr.hpp"
#include "game_audio.hpp"
#include "usb_storage.hpp"
#include "power_policy.hpp"
#include "settings_store.hpp"
#include "system_service.hpp"
#include "connectivity.hpp"
#include "ble_hid.hpp"
#include "display_cast.hpp"
#include "screenshot.hpp"
#include "password_vault.hpp"
#include "audio_recorder.hpp"
#include "audio_mgr.hpp"
#include "battery_mgr.hpp"
#include "photo_loader.hpp"
#include "storage_browser.hpp"
#include "sd_media.hpp"
#include "fc_emulator.hpp"
#include "lan_hotspot.hpp"
#include "speed_test.hpp"
#include "event_bus.hpp"
#include "bsp/power_port.h"
#include "esp_timer.h"
#include <atomic>

namespace sys {
namespace {
std::atomic<uint32_t> activitySerial{1},idleSerial{0};
std::atomic<bool> quiet{false},slow{false},stopRadios{false};
power::Policy policy; // GUI task owns the state machine
bool jobsBusy() {
    return !core::xiaozhi::idle() || !UsbStorage::idle() || !core::mp3::idle() || core::chip::busy() || core::game_audio::busy() || !vault::idle() || !core::storageIdle() || !core::photo::idle() ||
        core::sd::busy() || !core::recorder::idle() || !core::fc::idle() ||
        !Screenshot::idle() || !DisplayCast::idle() || !LanHotspot::idle() || !core::speed::idle() ||
        !BleHid::keysIdle() || !BleHid::mouseIdle() || !BleHid::privateInputIdle() ||
        SystemService::busy(ServiceDomain::Wifi) || SystemService::busy(ServiceDomain::Bluetooth) ||
        SystemService::busy(ServiceDomain::System) || SystemService::timeSyncBusy();
}
bool linkConnected() {return BleHid::connected() || Connectivity::wifiState()==WifiState::Connected;}
}
void Power::activity() {
    ++activitySerial;
    stopRadios=false;
    bsp_power_wake();
    SystemService::wake();
}
bool Power::saving() {return quiet.load();}
bool Power::automaticSleepAllowed() {return Settings::powerConfig().sleepOnPower || !Battery::supplyProtected();}
bool Power::automaticDeepSleepAllowed() {
    const auto c=Settings::powerConfig();
    return c.automatic && c.deepSeconds && (c.sleepOnPower || !Battery::supplyProtected()) &&
        !(c.protectLinks && linkConnected());
}
bool Power::automaticScreenOffAllowed() {return Settings::powerConfig().screenOffOnPower || !Battery::supplyProtected();}
bool Power::slowIdle() {return slow.load() && activitySerial.load()==idleSerial.load() && automaticSleepAllowed();}
void Power::pollAutomatic(uint32_t hints) {
    Battery::pollSupply(); // GPIO/cached USB only; independent of slow service I/O
    if(!automaticScreenOffAllowed())restoreAutomaticScreen();
    const auto config=Settings::powerConfig();
    static auto previous=power::encode(config);
    if(previous!=power::encode(config)) {
        previous=power::encode(config);
        if(isScreenOn())policy.reset(); // retain the blackout origin when settings change in the dark
    }
    const auto serial=activitySerial.load();
    const bool busy=jobsBusy();
    const auto wifi=Connectivity::snapshot();
    const auto bt=SystemService::bluetooth();
    const bool radioBusy=wifi.transitioning || wifi.state==WifiState::Searching || wifi.state==WifiState::Connecting ||
        wifi.scanPending || bt.state==BleHidState::Starting || bt.state==BleHidState::Connecting ||
        bt.scan==BleScanState::Scanning || bt.pairing!=BlePairAction::None;
    // While lit, short transactions keep the display awake. Once dark, every
    // session follows deadlines subject to explicit supply/link settings.
    const bool displayBusy=!UsbStorage::idle() || DisplayCast::enabled() || !DisplayCast::idle() ||
        !vault::idle() || !core::storageIdle() || !core::photo::idle() ||
        !Screenshot::idle() || !core::speed::idle() || !BleHid::keysIdle() || !BleHid::mouseIdle() || !BleHid::privateInputIdle();
    if(sleepPending()) {
        quiet=false;slow=false;stopRadios=false;setEconomy(false,false,false);bsp_power_set_quiet(false,isScreenOn());return;
    }
    auto d=policy.step(config,{uint32_t(esp_timer_get_time()/1000),serial,hints,isScreenOn(),displayBusy,busy||radioBusy,linkConnected(),Battery::supplyProtected()});
    if(d.black) {automaticScreenOff();d.stage=power::Stage::Dark;}
    quiet=d.stage==power::Stage::Quiet;
    const bool idle=automaticSleepAllowed() && (d.stage==power::Stage::Standby ||
        (quiet && !busy && !radioBusy && !(hints&power::KeepSession)));
    idleSerial=serial;slow=idle;
    stopRadios=d.radiosOff && !radioBusy && idle;
    // Modem-sleep/DFS is allowed at the dark deadline even with live sessions.
    // Active I/O keeps its work locks; do not force light sleep under a driver.
    setEconomy(idle,quiet && (hints&power::Dim),idle && !busy && !radioBusy);
    bsp_power_set_quiet(idle,isScreenOn());
    if(d.deep && !isScreenOn() && automaticDeepSleepAllowed()) (void)deepSleep(true);
}
uint32_t Power::activityGeneration() {return activitySerial.load();}
void Power::processEconomy() {
    if(!slowIdle() || sleepPending())return;
    (void)Audio::releaseIdle();
    if(!stopRadios.load() || !automaticSleepAllowed() || jobsBusy())return;
    const auto config=Settings::powerConfig();
    if(!config.longest || (config.protectLinks && linkConnected()))return;
    // Recheck after each blocking transition: fresh input cancels the remaining
    // request. The public drivers publish the same actual state used by Settings.
    if(Connectivity::snapshot().enabled)(void)Connectivity::setWifiEnabled(false);
    if(!stopRadios.load() || !automaticSleepAllowed() || !slowIdle() || jobsBusy())return;
    if(BleHid::enabled() && BleHid::setEnabled(false)==ESP_OK) {
        Settings::setBluetoothOn(false);EventBus::publish({EventType::BluetoothToggled,0});
    }
}
}
