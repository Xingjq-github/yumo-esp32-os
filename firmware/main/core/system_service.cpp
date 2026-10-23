#include "system_service.hpp"
#include "sdkconfig.h"
// This worker commits NVS from a PSRAM stack. Keep both flash code and rodata
// in PSRAM so flash operations do not disable the cache backing that stack.
#if !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA
#error "SystemService PSRAM stack requires flash instructions and rodata in PSRAM"
#endif
#include "connectivity.hpp"
#include "lan_hotspot.hpp"
#include "settings_store.hpp"
#include "clock_discipline.hpp"
#include "event_bus.hpp"
#include "audio_mgr.hpp"
#include "battery_mgr.hpp"
#include "power_mgr.hpp"
#include "sd_media.hpp"
#include "usb_storage.hpp"
#include "qmi8658.hpp"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <type_traits>
#include <sys/time.h>

namespace sys {
namespace {
// Radio initialization needs more stack than steady-state command handling.
// 6 KiB overflowed during BLE startup; keep the extra headroom in PSRAM.
constexpr uint32_t kWorkerStackBytes=16*1024;
struct Command { ServiceAction action; bool value=false; char ssid[33]{}; char password[65]{}; BleBond bond{}; BleScanDevice scanned{}; uint32_t pairGeneration=0, connectionGeneration=0; ConsumerKey mediaKey=ConsumerKey::PlayPause; uint8_t theme=0; int64_t epoch=0; };
StaticQueue_t control;
static_assert(std::is_trivially_copyable_v<Command>);
// Value payloads are task-only and may live in PSRAM; FreeRTOS queue control
// remains INTERNAL. Retained across retries for the worker's boot lifetime.
uint8_t *storage=nullptr;
QueueHandle_t queue=nullptr;
TaskHandle_t workerTask=nullptr;
std::atomic<bool> started{false}, cancelScan{false}, wifiScanCancelled{false};
std::atomic<unsigned> pending[3]{};
std::atomic<bool> wifiTogglePending{false};
std::atomic<uint32_t> connectionGeneration{0};
std::atomic<int> volume{-1};
std::mutex snapshotMutex;
ServiceResult results[3];
BluetoothSnapshot bluetoothState;
BleScanDevice *nearbyDevices=nullptr;
int nearbyCount=0;
bool sntpStarted=false;
std::atomic<bool> timeSyncActive{false},timeSyncQueued{false};
TimeSyncSnapshot timeState;
// Names must outlive lwIP's DNS callbacks. Keep one server at a time so no
// global Wi-Fi/lwIP configuration or reserved radio memory needs to change.
constexpr const char *timeServers[]={"ntp.aliyun.com","cn.pool.ntp.org","pool.ntp.org"};
constexpr int64_t kTimeAttemptUs=20000000LL,kTimeRetryUs=900000000LL,kTimeRefreshUs=3600000000LL;
int64_t timeDeadline=0,nextTimeSync=0;
// Worker-owned monotonic deadline; explicit time updates restart the minute
// checkpoint so a successful calibration is not followed by a redundant tick.
int64_t nextClockSave=0;
unsigned timeServer=0;
bool timeWasOnline=false;
BluetoothConnectionFlow bluetoothFlow;
// Boot intents are consumed once. Any explicit radio request supersedes them,
// including while initialization/host synchronization is still in progress.
std::atomic<bool> bootWifiAllowed{true},bootBluetoothAllowed{true};
bool bootWifiPending=false,bootBluetoothPending=false;
int64_t bootConnectDeadline=0;

bool startsBluetoothConnection(const Command &cmd) {
    return cmd.action==ServiceAction::BluetoothConnect || cmd.action==ServiceAction::BluetoothReconnect ||
        ((cmd.action==ServiceAction::BluetoothDiscover || cmd.action==ServiceAction::BluetoothSecureDiscover) && cmd.value);
}
void processBluetoothCancellation() {
    // Out-of-band cancellation cannot be lost even when the command queue is full.
    // Preserve authenticated links; cancel only unfinished invitations/scans.
    static uint32_t handled=0;
    const auto generation=connectionGeneration.load();
    if(generation==handled) return;
    bluetoothFlow.cancel();
    handled=generation;
}
esp_err_t enqueue(const Command &request) {
    if(!started.load() || Power::sleepPending()) return ESP_ERR_INVALID_STATE;
    if(request.action<=ServiceAction::WifiForget) bootWifiAllowed.store(false);
    if(request.action>=ServiceAction::BluetoothEnable && request.action<=ServiceAction::BluetoothForgetAll)
        bootBluetoothAllowed.store(false);
    Command cmd=request;
    cmd.connectionGeneration=connectionGeneration.load();
    auto index=static_cast<unsigned>(SystemService::domain(cmd.action));
    ++pending[index];
    const bool sent=xQueueSend(queue,&cmd,0)==pdTRUE;
    std::memset(cmd.password,0,sizeof(cmd.password));
    if(!sent) { --pending[index]; return SystemService::QueueFull; }
    SystemService::wake();
    return ESP_OK;
}
esp_err_t syncTime();
void checkpointClock(int64_t epoch) {
    Settings::saveClock(epoch);
    nextClockSave=esp_timer_get_time()+60000000LL;
}
void pollClockCheckpoint(int64_t now) {
    if(now>=nextClockSave) checkpointClock(time(nullptr));
}
void publishTime(TimeSyncState state,esp_err_t error=ESP_OK,int64_t epoch=0) {
    const std::lock_guard<std::mutex> lock(snapshotMutex);
    timeState.state=state;timeState.error=error;timeState.server=timeServer+1;
    timeState.epoch=epoch;++timeState.revision;
}
void stopTimeClient() {
    if(sntpStarted) {
        esp_netif_sntp_deinit();sntpStarted=false;
        // Consume even if disconnection/cancellation raced a successful step.
        // The TCP/IP deinit fence guarantees capture is complete before NVS work.
        ClockDiscipline::networkStopped();
    }
}
void finishTime(TimeSyncState state,esp_err_t error=ESP_OK) {
    // Stop callbacks and destroy the semaphore before allowing standby. A
    // previous request cannot update the clock after a manual setting.
    stopTimeClient();
    const bool synced=state==TimeSyncState::Synced;
    const auto epoch=synced?time(nullptr):0;
    if(synced) {checkpointClock(epoch);EventBus::publish({EventType::TimeSynced});}
    nextTimeSync=esp_timer_get_time()+(synced?kTimeRefreshUs:kTimeRetryUs);
    publishTime(state,error,epoch);
    timeSyncActive.store(false);
}
esp_err_t startTimeServer() {
    stopTimeClient(); // discard any previous completion token as well
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG(timeServers[timeServer]);
    config.start=false;
    auto err=esp_netif_sntp_init(&config);
    if(err==ESP_OK) {sntpStarted=true;err=esp_netif_sntp_start();}
    if(err!=ESP_OK) {finishTime(TimeSyncState::Failed,err);return err;}
    timeDeadline=esp_timer_get_time()+kTimeAttemptUs;
    publishTime(TimeSyncState::Running);
    return ESP_OK;
}
esp_err_t syncTime() {
    if(timeSyncActive.load()) return ESP_OK; // repeated clicks cannot extend the deadline
    if(Connectivity::wifiState()!=WifiState::Connected) {
        publishTime(TimeSyncState::Offline,ESP_ERR_INVALID_STATE);return ESP_ERR_INVALID_STATE;
    }
    timeSyncActive.store(true);timeServer=0;timeWasOnline=true;
    return startTimeServer();
}
void pollTimeSync() {
    const bool online=Connectivity::wifiState()==WifiState::Connected;
    const auto now=esp_timer_get_time();
    if(!online || Power::sleepPending()) {
        if(timeSyncActive.load()) finishTime(online?TimeSyncState::Cancelled:TimeSyncState::Offline,ESP_ERR_INVALID_STATE);
        if(!online) {timeWasOnline=false;nextTimeSync=0;}
        return;
    }
    if(!timeWasOnline) {timeWasOnline=true;nextTimeSync=0;}
    if(timeSyncActive.load()) {
        // Zero-tick poll: never block radio polling, UI commands or power work.
        const auto err=esp_netif_sntp_sync_wait(0);
        if(err==ESP_OK) {finishTime(TimeSyncState::Synced);return;}
        if(err!=ESP_ERR_TIMEOUT && err!=ESP_ERR_NOT_FINISHED) {finishTime(TimeSyncState::Failed,err);return;}
        if(now>=timeDeadline) {
            if(timeServer+1<sizeof(timeServers)/sizeof(timeServers[0])) {++timeServer;(void)startTimeServer();}
            else finishTime(TimeSyncState::Failed,ESP_ERR_TIMEOUT);
        }
    } else if(Power::isScreenOn() && now>=nextTimeSync) {
        (void)syncTime(); // reconnect and hourly refresh, never wake a dark device
    }
}
esp_err_t applyClock(int64_t epoch) {
    stopTimeClient();timeSyncActive.store(false);
    // Both manual setting and NTP are worker-owned, so a queued completion
    // cannot overwrite the value the user just selected.
    timeval tv{static_cast<time_t>(epoch),0};
    if(settimeofday(&tv,nullptr)!=0) {publishTime(TimeSyncState::Failed,ESP_FAIL);return ESP_FAIL;}
    ClockDiscipline::manualTimeChanged();
    timeWasOnline=Connectivity::wifiState()==WifiState::Connected;
    nextTimeSync=esp_timer_get_time()+kTimeRefreshUs;
    checkpointClock(epoch);EventBus::publish({EventType::TimeSynced});
    publishTime(TimeSyncState::Manual,ESP_OK,epoch);
    return Settings::flush(true)?ESP_OK:ESP_FAIL;
}
esp_err_t execute(const Command &cmd) try {
    processBluetoothCancellation();
    if(startsBluetoothConnection(cmd) && cmd.connectionGeneration!=connectionGeneration.load())
        return ESP_ERR_INVALID_STATE;
    switch(cmd.action) {
    case ServiceAction::WifiEnable:
        if(cmd.value) (void)Audio::releaseIdle();
        return Connectivity::setWifiEnabled(cmd.value);
    case ServiceAction::WifiConnect: return Connectivity::connectWifi(cmd.ssid,cmd.password);
    case ServiceAction::WifiDisconnect: return Connectivity::disconnectWifi();
    case ServiceAction::WifiScan: return wifiScanCancelled.load()?ESP_ERR_INVALID_STATE:Connectivity::startScan();
    case ServiceAction::WifiForget:
        if(Connectivity::wifiSsid()==cmd.ssid) (void)Connectivity::disconnectWifi();
        Settings::forgetWifi(cmd.ssid); return ESP_OK;
    case ServiceAction::BluetoothEnable: {
        if(!cmd.value) bluetoothFlow.cancel();
        auto err=BleHid::setEnabled(cmd.value);
        if(err==ESP_OK) { Settings::setBluetoothOn(cmd.value); EventBus::publish({EventType::BluetoothToggled,cmd.value}); }
        return err;
    }
    case ServiceAction::BluetoothDiscover:
    case ServiceAction::BluetoothSecureDiscover:
        if(!cmd.value) {bluetoothFlow.cancel();return ESP_OK;}
        return bluetoothFlow.begin(BluetoothOperation::Discover,{}, {},cmd.action==ServiceAction::BluetoothSecureDiscover,esp_timer_get_time());
    case ServiceAction::BluetoothScan: return cancelScan.load()?ESP_ERR_INVALID_STATE:BleHid::startScan();
    case ServiceAction::BluetoothDisconnect: bluetoothFlow.cancel();return BleHid::disconnect();
    case ServiceAction::BluetoothConnect:
        return bluetoothFlow.begin(BluetoothOperation::Keyboard,cmd.scanned.identity,cmd.scanned,false,esp_timer_get_time());
    case ServiceAction::BluetoothReconnect:
        return bluetoothFlow.begin(BluetoothOperation::Saved,cmd.bond,{},false,esp_timer_get_time());
    case ServiceAction::BluetoothForget: bluetoothFlow.cancelFor(cmd.bond);return BleHid::removeBond(cmd.bond);
    case ServiceAction::BluetoothForgetAll: bluetoothFlow.cancel();return BleHid::removeAllBonds();
    case ServiceAction::BluetoothMedia: return BleHid::sendConsumer(cmd.mediaKey);
    case ServiceAction::PairAnswer:
        return BleHid::confirmPairing(cmd.value,cmd.pairGeneration);
    case ServiceAction::SyncTime: return syncTime();
    case ServiceAction::SetTime: return applyClock(cmd.epoch);
    case ServiceAction::Motion:
        // One switch controls both automatic orientation and foreground games.
        // Neither consumer can keep the sensor on when permission is disabled.
        if(!Qmi8658::setEnabled(Qmi8658::requested(cmd.value) && Power::isScreenOn() && !Power::sleepPending())) return ESP_FAIL;
        Settings::setMotionSensorOn(cmd.value); return ESP_OK;
    case ServiceAction::Save: return Settings::flush(true)?ESP_OK:ESP_FAIL;
    case ServiceAction::DeepSleep: return Power::deepSleep();
    case ServiceAction::ThemeRestart: return Power::restartForTheme(static_cast<themes::Id>(cmd.theme));
    case ServiceAction::PowerOff:
        return Power::shutdown();
    }
    return ESP_ERR_INVALID_ARG;
} catch(const std::bad_alloc &) {
    return ESP_ERR_NO_MEM;
}
void bootResult(ServiceAction action,esp_err_t error) {
    const std::lock_guard<std::mutex> lock(snapshotMutex);
    auto &result=results[static_cast<unsigned>(SystemService::domain(action))];
    result.action=action;result.error=error;++result.revision;
}
void startBootRadios() {
    // Reserve Wi-Fi's controller memory before starting BLE. Both operations
    // use the regular rollback/error paths and never hold the display lock.
    if(bootWifiAllowed.load() && Settings::wifiOn() && !Power::sleepPending()) {
        const auto error=execute({ServiceAction::WifiEnable,true});
        bootWifiPending=error==ESP_OK;
        bootResult(ServiceAction::WifiEnable,error);
        if(error!=ESP_OK) ESP_LOGW("system","Wi-Fi boot enable failed: %s",esp_err_to_name(error));
    }
    if(bootBluetoothAllowed.load() && Settings::bluetoothOn()&&!Power::sleepPending()) {
        const auto error=execute({ServiceAction::BluetoothEnable,true});
        bootBluetoothPending=error==ESP_OK;
        bootResult(ServiceAction::BluetoothEnable,error);
        if(error!=ESP_OK) {
            Settings::setBluetoothOn(BleHid::enabled());
            ESP_LOGW("system","Bluetooth boot enable failed: %s",esp_err_to_name(error));
        }
    }
    bootConnectDeadline=esp_timer_get_time()+15000000LL;
}
void pollBootConnections() {
    if(!bootWifiPending && !bootBluetoothPending) return;
    if(Power::sleepPending() || Power::inStandby() || esp_timer_get_time()>=bootConnectDeadline) {
        bootWifiPending=bootBluetoothPending=false;return;
    }
    if(bootWifiPending) {
        if(!bootWifiAllowed.load() || !Connectivity::wifiEnabled() || Connectivity::hotspotReserved())
            bootWifiPending=false;
        else {
            const auto wifi=Connectivity::snapshot();
            if(!wifi.transitioning) {
                bootWifiPending=false; // allocation/driver failure must not become a polling retry
                if(wifi.state==WifiState::Disconnected) {
                    const auto ssid=Settings::wifiSsid();
                    if(!ssid.empty()) {
                        const auto error=Connectivity::connectWifi(ssid,Settings::wifiPassword());
                        bootResult(ServiceAction::WifiConnect,error);
                        ESP_LOGI("system","Wi-Fi boot connection requested: %s",esp_err_to_name(error));
                    }
                }
            }
        }
    }
    if(bootBluetoothPending) {
        if(!bootBluetoothAllowed.load() || !BleHid::enabled() || BleHid::state()==BleHidState::Failed)
            bootBluetoothPending=false;
        else if(BleHid::state()!=BleHidState::Starting) {
            bootBluetoothPending=false;
            if(BleHid::state()==BleHidState::Ready && bluetoothFlow.operation==BluetoothOperation::None && !BleHid::connected()) {
                BleBond target{};
                const auto selection=BleHid::bootBond(target);
                if(selection==ESP_OK) {
                    const auto error=bluetoothFlow.begin(BluetoothOperation::Saved,target,{},false,esp_timer_get_time());
                    bootResult(ServiceAction::BluetoothReconnect,error);
                    ESP_LOGI("system","Bluetooth boot connection requested: %s",esp_err_to_name(error));
                } else {
                    bootResult(ServiceAction::BluetoothReconnect,selection==ESP_ERR_NOT_FOUND?ESP_OK:selection);
                    ESP_LOGI("system","Bluetooth boot target selection: %s",esp_err_to_name(selection));
                }
            }
        }
    }
}
void refreshBluetooth(bool forceBonds=false) {
    static int64_t nextBondRefresh=0;
    BluetoothSnapshot previous;
    { const std::lock_guard<std::mutex> lock(snapshotMutex); previous=bluetoothState; }
    BluetoothSnapshot next=previous;
    next.peer=BleHid::peer();next.waitingForPeer=BleHid::waitingForPeer();
    next.error=BleHid::lastError(); next.enabled=BleHid::enabled(); next.state=BleHid::state(); next.scan=BleHid::scanState();
    next.operation=bluetoothFlow.operation;next.target=bluetoothFlow.target;next.switching=bluetoothFlow.switching;
    if(bluetoothFlow.error) next.error=bluetoothFlow.error;
    if(bluetoothFlow.error && next.enabled && !BleHid::connected()) next.state=BleHidState::Failed;
    const auto pairing=BleHid::pairing();
    next.pairing=pairing.action; next.passkey=pairing.passkey; next.pairGeneration=pairing.generation;
    next.full=BleHid::bondStoreFull(); next.discoverableSeconds=BleHid::discoverableSecondsRemaining();
    const auto now=esp_timer_get_time();
    if(forceBonds || now>=nextBondRefresh || next.state!=previous.state || next.enabled!=previous.enabled) {
        next.bondCount=BleHid::bonds(next.bonds.data(),next.bonds.size());
        nextBondRefresh=now+1000000; // state stays responsive; bond storage is not read at 10 Hz
    }
    // GAP may have disconnected while bonds were copied. A saved key is never
    // sufficient evidence for displaying an active connection.
    if(next.state==BleHidState::Connected && !BleHid::connected()) next.state=BleHidState::Ready;
    if(!next.enabled && next.state!=BleHidState::Failed) next.state=BleHidState::Disabled;
    next.peer.connected=next.enabled && next.state==BleHidState::Connected && BleHid::connected();
    if(!next.enabled || (next.state!=BleHidState::Connected && next.state!=BleHidState::Connecting)) next.peer={};
    // Bond-cache refresh is slower than GAP. Derive every row from the same
    // usable peer so old/new hosts cannot both appear connected for a second.
    for(int i=0;i<next.bondCount;++i)
        next.bonds[i].connected=next.peer.connected && BlePeerStore::same(next.peer,next.bonds[i]);
    BleScanDevice scanned[BleHid::kMaxScanDevices]{};
    const int count=BleHid::scanResults(scanned,BleHid::kMaxScanDevices);
    const std::lock_guard<std::mutex> lock(snapshotMutex);
    bool changed=next.bondCount!=bluetoothState.bondCount;
    for(int i=0;i<next.bondCount;++i)
        if(next.bonds[i].addrType!=bluetoothState.bonds[i].addrType || next.bonds[i].connected!=bluetoothState.bonds[i].connected ||
           std::memcmp(next.bonds[i].addr,bluetoothState.bonds[i].addr,6)!=0 ||
           next.bonds[i].keyboard!=bluetoothState.bonds[i].keyboard || std::strcmp(next.bonds[i].name,bluetoothState.bonds[i].name)!=0) changed=true;
    next.bondRevision=bluetoothState.bondRevision+(changed?1:0);
    bool scanChanged=count!=nearbyCount || next.scan!=bluetoothState.scan;
    for(int i=0;i<count;++i)
        if(scanned[i].rssi!=nearbyDevices[i].rssi || scanned[i].address.addrType!=nearbyDevices[i].address.addrType ||
           std::memcmp(scanned[i].address.addr,nearbyDevices[i].address.addr,6)!=0 || scanned[i].connectable!=nearbyDevices[i].connectable ||
           std::strcmp(scanned[i].name,nearbyDevices[i].name)!=0) scanChanged=true;
    next.scanRevision=bluetoothState.scanRevision+(scanChanged?1:0);
    nearbyCount=count; std::copy_n(scanned,count,nearbyDevices); bluetoothState=next;
}
void worker(void *) {
    // Never repeat initialization after manual/economy shutdown.
    try {
        Power::WorkGuard startupWork;
        if(!Qmi8658::powerDownAtBoot()) ESP_LOGW("system","IMU startup power-down unavailable");
        startBootRadios();
        refreshBluetooth(true);
    } catch(const std::bad_alloc &) {
        ESP_LOGW("system","Insufficient memory during startup; manual retry available");
    }
    int64_t nextBluetooth=0,nextBattery=0;
    int64_t nextMotionAttempt=0;
    bool previousMotionWanted=false;
    for(;;) {
        if(Power::sleepTerminal()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        bool standby=false;
        try {
            Power::WorkGuard serviceWork; // released before the idle delay, including early exits
            const auto supplyNow=esp_timer_get_time();
            if(supplyNow>=nextBattery) {nextBattery=supplyNow+1000000LL;Battery::poll(Power::isScreenOn());}
            if(wifiScanCancelled.load()) Connectivity::cancelUserScan();
            Connectivity::poll();
            pollTimeSync(); // acquire bounded busy protection before economy can turn Wi-Fi off
            LanHotspot::poll(); // foreground-only lease; close bypasses the command queue
            core::sd::pollEject(); // only pending eject; never auto-probe an ejected card
            processBluetoothCancellation();
            BleHid::poll();
            bluetoothFlow.poll(esp_timer_get_time(),Power::sleepPending());
            Power::processEconomy();
            if(cancelScan.load() && BleHid::scanState()==BleScanState::Scanning) BleHid::cancelUserScan();
            Command cmd{};
            // Bound work per iteration; coalesced volume and state updates cannot starve.
            for(unsigned i=0;i<4 && xQueueReceive(queue,&cmd,0)==pdTRUE;++i) {
                auto err=Power::sleepPending()?ESP_ERR_INVALID_STATE:execute(cmd);
                auto index=static_cast<unsigned>(SystemService::domain(cmd.action));
                { const std::lock_guard<std::mutex> lock(snapshotMutex);
                  results[index].action=cmd.action; results[index].error=err; ++results[index].revision; }
                if(cmd.action==ServiceAction::WifiEnable) wifiTogglePending.store(false);
                if(cmd.action==ServiceAction::SyncTime) timeSyncQueued.store(false);
                --pending[index];
                std::memset(cmd.password,0,sizeof(cmd.password));
                if(index==static_cast<unsigned>(ServiceDomain::Bluetooth)) {
                    processBluetoothCancellation();
                    BleHid::poll(); bluetoothFlow.poll(esp_timer_get_time(),Power::sleepPending());refreshBluetooth(true); nextBluetooth=0;
                }
            }
            // Explicit queued commands win over boot intents; no App pointers
            // or second worker/timer are retained for automatic connection.
            pollBootConnections();
            int desired=volume.exchange(-1);
            if(desired>=0 && !Power::sleepPending() && !Audio::applyVolume(desired)) ESP_LOGW("system","Speaker volume could not be applied");
            // Complete transitions without halting radio polling, timers or wake handling.
            // Commands accepted before entry drain first; late racing commands are rejected.
            if(Power::sleepPending() && timeSyncActive.load()) finishTime(TimeSyncState::Cancelled);
            if(Power::sleepPending() && uxQueueMessagesWaiting(queue)==0) Power::processStandby();
            standby=Power::inStandby();
            if(Power::sleepTerminal()) continue; // release the work lease before the terminal wait
            const auto now=esp_timer_get_time();
            const bool motionWanted=Qmi8658::requested(Settings::motionSensorOn()) && Power::isScreenOn() && !Power::slowIdle() && !Power::sleepPending();
            if(motionWanted!=previousMotionWanted) { previousMotionWanted=motionWanted;nextMotionAttempt=0; }
            if(motionWanted!=Qmi8658::enabled() && now>=nextMotionAttempt) {
                if(Qmi8658::setEnabled(motionWanted)) nextMotionAttempt=0;
                else { nextMotionAttempt=now+2000000;ESP_LOGW("system","Motion state change failed"); }
            }
            if(motionWanted) Qmi8658::pollTemporaryAcceleration();
            if(now>=nextBluetooth) { nextBluetooth=now+(standby || Power::slowIdle()?1000000LL:100000LL); refreshBluetooth(); }
            pollClockCheckpoint(now);
            (void)Settings::flush();
        } catch(const std::bad_alloc &) {
            // Command accounting is completed before optional snapshot refresh.
            static int64_t nextMemoryWarning=0;
            const auto now=esp_timer_get_time();
            if(now>=nextMemoryWarning){nextMemoryWarning=now+2000000;ESP_LOGW("system","Insufficient memory; service poll deferred");}
        } // serviceWork: no frequency lock held across the periodic idle wait
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(standby?100:Power::slowIdle()?1000:50));
    }
}
}
ServiceDomain SystemService::domain(ServiceAction action) {
    if(action<=ServiceAction::WifiForget) return ServiceDomain::Wifi;
    if(action<=ServiceAction::PairAnswer) return ServiceDomain::Bluetooth;
    return ServiceDomain::System;
}
bool SystemService::start() {
    if(started.load()) return true;
    if (!nearbyDevices) nearbyDevices=static_cast<BleScanDevice *>(heap_caps_calloc(
        BleHid::kMaxScanDevices,sizeof(BleScanDevice),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!nearbyDevices) return false;
    if(!storage) storage=static_cast<uint8_t *>(heap_caps_malloc(8*sizeof(Command),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!storage) return false;
    queue=xQueueCreateStatic(8,sizeof(Command),storage,&control);
    if(!queue) return false;
    started.store(true);
    if(xTaskCreateWithCaps(worker,"sys_service",kWorkerStackBytes,nullptr,4,&workerTask,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        started.store(false); return false;
    }
    return true;
}
bool SystemService::running() { return started.load(); }
void SystemService::wake() {if(workerTask)xTaskNotifyGive(workerTask);}
esp_err_t SystemService::switchTheme(uint8_t id) {
    if(!themes::valid(id)) return ESP_ERR_INVALID_ARG;
    Command cmd{ServiceAction::ThemeRestart}; cmd.theme=id; return enqueue(cmd);
}
esp_err_t SystemService::request(ServiceAction action,bool value) {
    if(action==ServiceAction::SetTime) return ESP_ERR_INVALID_ARG; // use validated value payload
    if(action==ServiceAction::SyncTime && (timeSyncActive.load() || timeSyncQueued.exchange(true))) return ESP_OK;
    if(action==ServiceAction::BluetoothDisconnect || (action==ServiceAction::BluetoothEnable && !value) ||
       ((action==ServiceAction::BluetoothDiscover || action==ServiceAction::BluetoothSecureDiscover) && !value))
        cancelBluetoothConnection();
    if(action==ServiceAction::BluetoothScan) cancelScan.store(false);
    if(action==ServiceAction::WifiScan) wifiScanCancelled.store(false);
    if(action==ServiceAction::WifiEnable && wifiTogglePending.exchange(true)) return ESP_ERR_NOT_FINISHED;
    const auto error=enqueue({action,value});
    if(action==ServiceAction::WifiEnable && error!=ESP_OK) wifiTogglePending.store(false);
    if(action==ServiceAction::SyncTime && error!=ESP_OK) timeSyncQueued.store(false);
    return error;
}
esp_err_t SystemService::connectWifi(const std::string &ssid,const std::string &password) {
    auto err=Connectivity::validateCredentials(ssid,password);
    if(err!=ESP_OK) return err;
    Command cmd{ServiceAction::WifiConnect};
    std::memcpy(cmd.ssid,ssid.data(),ssid.size()); std::memcpy(cmd.password,password.data(),password.size());
    err=enqueue(cmd); std::memset(cmd.password,0,sizeof(cmd.password)); return err;
}
esp_err_t SystemService::forgetWifi(const std::string &ssid) {
    if(ssid.empty() || ssid.size()>32) return ESP_ERR_INVALID_ARG;
    Command cmd{ServiceAction::WifiForget}; std::memcpy(cmd.ssid,ssid.data(),ssid.size()); return enqueue(cmd);
}
esp_err_t SystemService::connectKeyboard(const BleScanDevice &device) { Command cmd{ServiceAction::BluetoothConnect};cmd.scanned=device;return enqueue(cmd); }
esp_err_t SystemService::reconnectBond(const BleBond &device) { Command cmd{ServiceAction::BluetoothReconnect};cmd.bond=device;return enqueue(cmd); }
esp_err_t SystemService::forgetBond(const BleBond &bond) { Command cmd{ServiceAction::BluetoothForget}; cmd.bond=bond; return enqueue(cmd); }
esp_err_t SystemService::answerPairing(bool accept,uint32_t generation) { Command cmd{ServiceAction::PairAnswer,accept}; cmd.pairGeneration=generation; return enqueue(cmd); }
esp_err_t SystemService::sendConsumer(ConsumerKey key) {
    if(!BleHid::remoteConnected()) return ESP_ERR_INVALID_STATE;
    Command cmd{ServiceAction::BluetoothMedia}; cmd.mediaKey=key; return enqueue(cmd);
}
void SystemService::cancelBluetoothConnection() { bootBluetoothAllowed.store(false);++connectionGeneration; }
void SystemService::cancelWifiScan() { wifiScanCancelled.store(true); }
void SystemService::cancelBluetoothScan() { cancelScan.store(true); }
void SystemService::setVolume(int value) {
    if(Power::sleepPending()) return;
    value=std::max(0,std::min(100,value)); Settings::setVolume(value); volume.store(value);
    wake();
    EventBus::publish({EventType::VolumeChanged,value});
}
const char *SystemService::errorText(esp_err_t error) {
    switch(error) {
    case ESP_OK: return "";
    case QueueFull: return "操作队列已满，请稍后重试";
    case Connectivity::HotspotBusy: return "热点正在使用无线，请关闭局域网后重试";
    case ESP_ERR_NO_MEM: return "可用内存不足，请结束应用后重试";
    case ESP_ERR_NOT_FINISHED: return "操作正在处理，请稍候";
    case ESP_ERR_INVALID_ARG: return "输入格式不正确，请检查";
    case ESP_ERR_INVALID_STATE: return "当前不可操作，请检查开关或等待完成";
    case ESP_ERR_TIMEOUT: return "操作超时，请重试";
    default: return nullptr;
    }
}
bool SystemService::busy(ServiceDomain d) { return pending[static_cast<unsigned>(d)].load()!=0; }
esp_err_t SystemService::setClock(int64_t epoch) {
    if(epoch<1577836800LL || epoch>=4102444800LL) return ESP_ERR_INVALID_ARG;
    Command cmd{ServiceAction::SetTime};cmd.epoch=epoch;return enqueue(cmd);
}
bool SystemService::timeSyncBusy() {return timeSyncActive.load() || timeSyncQueued.load();}
TimeSyncSnapshot SystemService::timeSync() {const std::lock_guard<std::mutex> lock(snapshotMutex);return timeState;}
ServiceResult SystemService::result(ServiceDomain d) { const std::lock_guard<std::mutex> lock(snapshotMutex); return results[static_cast<unsigned>(d)]; }
BluetoothSnapshot SystemService::bluetooth() { const std::lock_guard<std::mutex> lock(snapshotMutex); return bluetoothState; }
int SystemService::nearby(BleScanDevice *out,int capacity) {
    if(!out || capacity<=0) return 0;
    const std::lock_guard<std::mutex> lock(snapshotMutex); int count=std::min(capacity,nearbyCount); if(count) std::copy_n(nearbyDevices,count,out); return count;
}
} // namespace sys
