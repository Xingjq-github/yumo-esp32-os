#include "connectivity.hpp"
#include "settings_store.hpp"
#include "power_mgr.hpp"
#include "event_bus.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <new>

namespace sys {
namespace {
constexpr int64_t kScanTimeout = 12000000;
constexpr int64_t kConnectTimeout = 20000000, kAddressTimeout = 15000000;
constexpr uint32_t kActiveScanMinMs=120, kActiveScanMaxMs=300;
constexpr int64_t kDriverBusyTimeout = 2000000, kProbeRetryInterval = 250000;
constexpr int64_t kPresenceMaxAge = 60000000;
std::mutex mutex;
WifiSnapshot state;
wifi_ap_record_t *networks=nullptr;
uint16_t networkCount=0;
std::string targetSsid, targetPassword;
bool initialized=false, driverInitialized=false, reconnect=false, ignoreDisconnect=false;
// A connect call owns the driver until DISCONNECTED or STA_STOP, even if the
// UI already reports a timeout. Never infer that ownership from the UI state.
bool connectionActive=false, probeRecovered=false;
std::atomic<bool> hotspotOwner{false};
bool hotspotShared=false; // protected by mutex; never deinit a borrowed STA
esp_netif_t *station=nullptr;
// A cancelled scan retains ownership until its terminal callback is consumed.
// Completion has a separate mailbox, so ordinary radio-event overflow cannot
// lose it. STA_STOP is a recovery fence for a timed-out / failed driver scan.
bool scanOutstanding=false, awaitingStop=false;
bool awaitingStart=false, restartPending=false, connectQueued=false;
bool targetScan=false, targetValidated=false;
bool broadcastProbe=false;
int64_t networksSeenAt=-1, targetSeenAt=-1;
uint8_t targetChannel=0;
int targetRssi=0;
std::atomic<bool> startDelivered{false}, driverStarted{false};
std::atomic<int> scanCompletion{-1};
std::atomic<bool> stopDelivered{false};
int64_t connectDeadline=0, scanDeadline=0, retryAt=0;
int64_t cancelDeadline=0, transitionDeadline=0;
int64_t disconnectDeadline=0, probeRetryAt=0, probeBusyDeadline=0;
int64_t attemptStarted=-1, probeStarted=-1, joinStarted=-1, addressStarted=-1;
bool transitionTimedOut=false;
std::mutex viewMutex;
WifiSnapshot view;
wifi_ap_record_t *viewNetworks=nullptr;
uint16_t viewCount=0;
struct PublishView {
    ~PublishView() {
        try {
            const std::lock_guard<std::mutex> lock(viewMutex);
            // Reserve before modifying any published field. If memory is low,
            // retain the complete previous snapshot and retry on the next poll;
            // an allocation escaping this scope guard would call terminate.
            view.ssid.reserve(state.ssid.size());
            view.ip.reserve(state.ip.size());
            const bool scansChanged=view.scanRevision!=state.scanRevision || viewCount!=networkCount;
            view=state; view.transitioning=!transitionTimedOut && (awaitingStart || awaitingStop || restartPending || ignoreDisconnect); view.retrying=retryAt!=0; view.scanPending=scanOutstanding || awaitingStop;
            // Scan arrays change at scan completion/clear, not at every 50ms
            // service poll. Keep the public scan revision/count as the fence.
            if(scansChanged && networkCount) std::copy_n(networks,networkCount,viewNetworks);
            viewCount=networkCount;
        } catch(const std::bad_alloc &) {
            // No allocating diagnostics from a destructor, including unwind.
        }
    }
};

struct RadioEvent { enum Kind { Disconnected, Associated, GotIp, LostIp } kind; int detail; char ssid[33]{}; };
StaticQueue_t eventControl;
uint8_t eventStorage[16*sizeof(RadioEvent)];
QueueHandle_t events=nullptr;
esp_event_handler_instance_t wifiHandler=nullptr, ipHandler=nullptr;
std::atomic<bool> overflow{false};

void resetConnectionTiming(int64_t now=-1) {
    attemptStarted=now; probeStarted=joinStarted=addressStarted=-1;
}
long long elapsedMs(int64_t started,int64_t now) {
    return started>=0 && now>=started?static_cast<long long>((now-started)/1000):-1;
}
void reportStartFailure(const char *stage,esp_err_t error) {
    // Failure only: enough evidence for the next reproduction, no polling log
    // or credentials. PSRAM totals do not describe the radio's internal heap.
    ESP_LOGE("wifi","%s: %s; internal free=%u largest=%u",stage,esp_err_to_name(error),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
}
// mutex held. Only release a stopped STA owned here, never the hotspot driver.
// A STOP callback is the fence for outstanding scans and queued radio events.
esp_err_t releaseStoppedLocked() {
    if(hotspotShared)return ESP_ERR_NOT_FINISHED;
    if(awaitingStop || awaitingStart || driverStarted.load()) return ESP_ERR_NOT_FINISHED;
    // Deinit can partially tear down the SDK before failing. Do not reuse this
    // initialization; the next explicit request must finish cleanup first.
    initialized=false;
    if(driverInitialized) {
        const auto err=esp_wifi_deinit();
        if(err!=ESP_OK) return err; // retain ownership so a later user request can retry
        driverInitialized=false;
    }
    if(wifiHandler) {
        const auto err=esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifiHandler);
        if(err!=ESP_OK) return err;
        wifiHandler=nullptr;
    }
    if(ipHandler) {
        const auto err=esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ipHandler);
        if(err!=ESP_OK) return err;
        ipHandler=nullptr;
    }
    // Unregister waits for callbacks in the event loop before their storage is reset.
    if(station) { esp_netif_destroy_default_wifi(station); station=nullptr; }
    if(events) xQueueReset(events);
    networkCount=0; heap_caps_free(networks); networks=nullptr;
    { const std::lock_guard<std::mutex> viewLock(viewMutex);
      viewCount=0; heap_caps_free(viewNetworks); viewNetworks=nullptr; }
    reconnect=false; ignoreDisconnect=false;
    connectionActive=probeRecovered=false;
    awaitingStart=awaitingStop=restartPending=connectQueued=scanOutstanding=false;
    targetScan=targetValidated=transitionTimedOut=false;
    broadcastProbe=false; networksSeenAt=targetSeenAt=-1; targetChannel=0; targetRssi=0;
    connectDeadline=scanDeadline=retryAt=cancelDeadline=transitionDeadline=0;
    disconnectDeadline=probeRetryAt=probeBusyDeadline=0;
    resetConnectionTiming();
    startDelivered.store(false); stopDelivered.store(false); driverStarted.store(false);
    scanCompletion.store(-1); overflow.store(false);
    targetSsid.clear(); targetPassword.clear();
    // Observers compare revisions across disable/re-enable and STA/AP hand-off.
    const auto revision=state.revision+1,scanRevision=state.scanRevision+1;
    state=WifiSnapshot{}; state.revision=revision; state.scanRevision=scanRevision;
    Settings::setWifiOn(false);
    return ESP_OK;
}
void setState(WifiState value,int error=0) {
    if(state.state==value && state.error==error) return;
    if(value==WifiState::Connected) state.phase=WifiConnectPhase::Online;
    else if(value!=WifiState::Connecting && value!=WifiState::Failed) state.phase=WifiConnectPhase::None;
    state.state=value; state.error=error; ++state.revision;
    EventType type=EventType::WifiDisconnected;
    if(value==WifiState::Connecting) type=EventType::WifiConnecting;
    if(value==WifiState::Connected) type=EventType::WifiConnected;
    if(value==WifiState::Failed) type=EventType::WifiFailed;
    EventBus::publish({type,error});
}
esp_err_t startFailed(const char *stage,esp_err_t error) {
    reportStartFailure(stage,error);
    const auto cleanup=releaseStoppedLocked();
    if(cleanup!=ESP_OK) reportStartFailure("rollback",cleanup);
    state.enabled=false; Settings::setWifiOn(false);
    setState(WifiState::Failed,error); // keep the initiating failure visible
    return error;
}
void radioEvent(void *,esp_event_base_t base,int32_t id,void *data) {
    RadioEvent event{};
    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {
        scanCompletion.store(static_cast<int>(static_cast<wifi_event_sta_scan_done_t *>(data)->status));
        return;
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) {
        driverStarted.store(true); startDelivered.store(true);
        return;
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP) {
        driverStarted.store(false); stopDelivered.store(true);
        return;
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        const auto *info=static_cast<wifi_event_sta_disconnected_t *>(data);
        event={RadioEvent::Disconnected,info->reason};
        std::memcpy(event.ssid,info->ssid,std::min<size_t>(info->ssid_len,32));
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED) {
        const auto *info=static_cast<wifi_event_sta_connected_t *>(data);
        event={RadioEvent::Associated,0};
        std::memcpy(event.ssid,info->ssid,std::min<size_t>(info->ssid_len,32));
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) event={RadioEvent::GotIp,0};
    else if(base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) event={RadioEvent::LostIp,0};
    else return;
    if(xQueueSend(events,&event,0)!=pdTRUE) overflow.store(true);
}
int appliedPowerSave=-1;
void tunePowerSave() {
    const auto mode=Power::automaticSleepAllowed()?WIFI_PS_MIN_MODEM:WIFI_PS_NONE;
    if(appliedPowerSave==int(mode))return;
    const auto err=esp_wifi_set_ps(mode);
    if(err==ESP_OK)appliedPowerSave=int(mode);
}
void tuneRadio() {
    (void)esp_wifi_set_max_tx_power(52); // preserve battery/brownout mitigation
    appliedPowerSave=-1;tunePowerSave();
}
void stopScanLocked() {
    if(!initialized) return;
    if(state.scan==WifiScanState::Scanning && scanOutstanding && scanCompletion.load()<0) (void)esp_wifi_scan_stop();
    (void)esp_wifi_clear_ap_list();
    state.scan=WifiScanState::Idle; state.scanError=ESP_OK;
    scanDeadline=0; targetScan=false; ++state.scanRevision;
    if(scanOutstanding) cancelDeadline=esp_timer_get_time()+2000000;
}
esp_err_t requestDisconnectLocked() {
    if(ignoreDisconnect) {
        // A previous recovery may have failed to stop the driver. Only a new
        // explicit request re-arms that wait; poll must not retry it forever.
        if(!disconnectDeadline && !awaitingStop && !awaitingStart) {
            disconnectDeadline=esp_timer_get_time()+kDriverBusyTimeout;
            transitionTimedOut=false;
        }
        return ESP_OK; // the existing terminal event still owns this fence
    }
    const auto err=esp_wifi_disconnect();
    if(err==ESP_ERR_WIFI_NOT_CONNECT || err==ESP_ERR_WIFI_NOT_STARTED) {
        connectionActive=false; disconnectDeadline=0;
        return ESP_OK;
    }
    if(err==ESP_OK && connectionActive) {
        ignoreDisconnect=true;
        disconnectDeadline=esp_timer_get_time()+kDriverBusyTimeout;
    }
    return err;
}
esp_err_t restartStationLocked(int64_t now) {
    // A whole-driver restart would also tear down the active local network.
    // Retain failed STA fences until the hotspot closes or the event arrives.
    if(hotspotShared)return Connectivity::HotspotBusy;
    const auto err=esp_wifi_stop();
    if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_STARTED) return err;
    awaitingStop=true; awaitingStart=false; startDelivered.store(false);
    restartPending=state.enabled; transitionTimedOut=false; transitionDeadline=now+4000000;
    if(err==ESP_ERR_WIFI_NOT_STARTED) { driverStarted.store(false); stopDelivered.store(true); }
    connectDeadline=disconnectDeadline=scanDeadline=cancelDeadline=probeRetryAt=probeBusyDeadline=0;
    targetScan=false; targetValidated=false; connectQueued=reconnect && state.enabled && !retryAt;
    broadcastProbe=false; networksSeenAt=targetSeenAt=-1; targetChannel=0; targetRssi=0;
    state.scan=WifiScanState::Idle; state.scanError=ESP_OK; ++state.scanRevision;
    state.ip.clear(); state.rssi=0;
    if(!state.enabled) setState(WifiState::Off);
    else if(connectQueued) setState(WifiState::Searching);
    else if(state.state!=WifiState::Failed) setState(WifiState::Disconnected);
    return ESP_OK; // keep credentials; STOP and START fence the next presence probe
}
esp_err_t beginConnect() {
    // scan_start's per-call dwell does not change the driver's internal
    // connection scan. Apply the same policy here, after START/scan/disconnect
    // fences; the SDK rejects this API while a scan or connection is active.
    wifi_scan_default_params_t scan{};
    scan.scan_time.active.min=kActiveScanMinMs;
    scan.scan_time.active.max=kActiveScanMaxMs;
    scan.scan_time.passive=WIFI_PASSIVE_SCAN_DEFAULT_TIME;
    scan.home_chan_dwell_time=WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME;
    auto err=esp_wifi_set_scan_parameters(&scan);
    if(err!=ESP_OK) return err;
    wifi_config_t config{};
    // SSID may occupy all 32 bytes; a hexadecimal PSK may occupy all 64.
    std::memcpy(config.sta.ssid,targetSsid.data(),targetSsid.size());
    std::memcpy(config.sta.password,targetPassword.data(),targetPassword.size());
    config.sta.threshold.authmode=targetPassword.empty()?WIFI_AUTH_OPEN:WIFI_AUTH_WPA_PSK;
    config.sta.pmf_cfg.capable=true;
    // A fresh presence scan already selected the strongest observed channel.
    // ALL_CHANNEL_SCAN ignores that head start and still scans every channel.
    // Try the observed channel first; retain an unpinned, all-channel fallback
    // on bounded retries so a changed channel or another mesh AP can recover.
    const bool fastScan=state.retry==0 && targetChannel>=1 && targetChannel<=13;
    config.sta.scan_method=fastScan?WIFI_FAST_SCAN:WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    // SDK per-candidate retries apply only to ALL_CHANNEL_SCAN.
    config.sta.failure_retry_cnt=fastScan?0:1;
    // Start discovery on the channel just observed, without pinning a BSSID:
    // mesh/repeater APs with the same SSID can still be selected by the driver.
    config.sta.channel=targetChannel;
    networksSeenAt=targetSeenAt=-1; // retries require new presence evidence
    err=esp_wifi_set_config(WIFI_IF_STA,&config);
    if(err==ESP_OK) {
        joinStarted=esp_timer_get_time(); addressStarted=-1;
        ESP_LOGI("wifi","join start attempt=%u scan=%s channel=%u prejoin_ms=%lld",
            static_cast<unsigned>(state.retry)+1,fastScan?"fast":"all",static_cast<unsigned>(targetChannel),
            elapsedMs(attemptStarted,joinStarted));
        err=esp_wifi_connect();
    }
    state.ssid=targetSsid; state.ip.clear(); state.rssi=targetRssi;
    state.phase=WifiConnectPhase::Joining;
    if(err==ESP_OK) { connectionActive=true; connectDeadline=esp_timer_get_time()+kConnectTimeout; setState(WifiState::Connecting); }
    else { connectDeadline=0; setState(WifiState::Failed,err); }
    return err;
}
esp_err_t beginTargetScan() {
    if(scanOutstanding || awaitingStop || awaitingStart) return ESP_ERR_INVALID_STATE;
    wifi_scan_config_t config{};
    if(!broadcastProbe) config.ssid=reinterpret_cast<uint8_t *>(const_cast<char *>(targetSsid.c_str()));
    config.show_hidden=true; // Directed active probe also supports a saved hidden SSID.
    // Leave enough dwell time for coexistence/busy APs. A directed miss gets
    // one unfiltered all-channel scan before we declare the network absent.
    config.scan_time.active.min=kActiveScanMinMs; config.scan_time.active.max=kActiveScanMaxMs;
    (void)esp_wifi_clear_ap_list();
    auto err=esp_wifi_scan_start(&config,false);
    // A previous operation can still occupy the radio. This is a bounded
    // scheduling wait, not a failed authentication or an absent saved network.
    if(err==ESP_ERR_WIFI_STATE) return err;
    targetScan=err==ESP_OK; scanOutstanding=targetScan;
    state.scan=targetScan?WifiScanState::Scanning:WifiScanState::Failed;
    state.scanError=err; ++state.scanRevision;
    scanDeadline=targetScan?esp_timer_get_time()+kScanTimeout:0;
    if(targetScan) {
        probeStarted=esp_timer_get_time();
        ESP_LOGI("wifi","probe start attempt=%u mode=%s elapsed_ms=%lld",
            static_cast<unsigned>(state.retry)+1,broadcastProbe?"broadcast":"directed",elapsedMs(attemptStarted,probeStarted));
    }
    return err;
}
void failed(int reason) {
    ESP_LOGW("wifi","connect failure state=%d phase=%d reason=%d channel=%u rssi=%d attempt=%u elapsed_ms=%lld; internal free=%u largest=%u",
        static_cast<int>(state.state),static_cast<int>(state.phase),reason,static_cast<unsigned>(targetChannel),state.rssi,
        static_cast<unsigned>(state.retry)+1,elapsedMs(attemptStarted,esp_timer_get_time()),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    resetConnectionTiming();
    connectDeadline=0; state.ip.clear(); state.rssi=0;
    probeRetryAt=probeBusyDeadline=0;
    setState(WifiState::Failed,reason);
    connectQueued=false; targetValidated=false;
    broadcastProbe=false; networksSeenAt=targetSeenAt=-1; targetChannel=0; targetRssi=0;
    const bool auth=reason==WIFI_REASON_AUTH_FAIL || reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason==WIFI_REASON_HANDSHAKE_TIMEOUT;
    if(reconnect && !targetSsid.empty() && !auth && state.retry<3) {
        retryAt=esp_timer_get_time()+((reason==Connectivity::NetworkNotSeen?30000000LL:2000000LL)<<state.retry); ++state.retry;
    } else if(hotspotShared && reconnect && !targetSsid.empty() && !auth) {
        // A shared hotspot can outlive an upstream outage. Continue slow,
        // bounded attempts while its lease exists; never retry denied credentials.
        retryAt=esp_timer_get_time()+60000000LL;state.retry=0;
    } else { retryAt=0; reconnect=false; } // recovery must not resurrect an exhausted/denied attempt
}
bool sameSsid(const uint8_t *ssid,const std::string &name) {
    return strnlen(reinterpret_cast<const char *>(ssid),32)==name.size() &&
        std::memcmp(ssid,name.data(),name.size())==0;
}
bool recentPresence(int64_t seenAt) {
    const auto now=esp_timer_get_time();
    return seenAt>=0 && now>=seenAt && now-seenAt<=kPresenceMaxAge;
}
void validateCachedTarget() {
    if(!recentPresence(networksSeenAt)) return;
    for(uint16_t i=0;i<networkCount;++i) if(sameSsid(networks[i].ssid,targetSsid)) {
        targetValidated=true; targetSeenAt=networksSeenAt; targetChannel=networks[i].primary; targetRssi=networks[i].rssi;
        break;
    }
}
void retainNetwork(const wifi_ap_record_t &ap) {
    if(!ap.ssid[0]) return;
    for(uint16_t i=0;i<networkCount;++i) if(std::strncmp(reinterpret_cast<const char *>(networks[i].ssid),reinterpret_cast<const char *>(ap.ssid),32)==0) {
        if(ap.rssi>networks[i].rssi) networks[i]=ap;
        return;
    }
    if(networkCount<Connectivity::kMaxNetworks) { networks[networkCount++]=ap; return; }
    auto weakest=std::min_element(networks,networks+networkCount,[](const wifi_ap_record_t &a,const wifi_ap_record_t &b){return a.rssi<b.rssi;});
    if(ap.rssi>weakest->rssi) *weakest=ap;
}
void consumeScan(int status) {
    if(state.scan!=WifiScanState::Scanning) { (void)esp_wifi_clear_ap_list(); return; }
    scanDeadline=0; networkCount=0; networksSeenAt=-1;
    uint16_t count=0;
    bool found=false;
    int foundRssi=-128;
    esp_err_t err=status==0?esp_wifi_scan_get_ap_num(&count):ESP_FAIL;
    for(uint16_t i=0;err==ESP_OK && i<count;++i) {
        // Consume/free one driver record at a time. The 24-row UI limit must
        // never limit the presence check or require a second large allocation.
        wifi_ap_record_t ap{};
        err=esp_wifi_scan_get_ap_record(&ap);
        if(err!=ESP_OK) break;
        if(targetScan && sameSsid(ap.ssid,targetSsid) && (!found || ap.rssi>foundRssi)) {
            found=true; foundRssi=ap.rssi; targetChannel=ap.primary; targetRssi=ap.rssi;
        }
        retainNetwork(ap);
    }
    if(err==ESP_OK) {
        std::sort(networks,networks+networkCount,[](const wifi_ap_record_t &a,const wifi_ap_record_t &b){return a.rssi>b.rssi;});
        networksSeenAt=esp_timer_get_time();
    } else networkCount=0; // incomplete/error results are not presence evidence
    // ScanDone, not a nonzero AP count, is the completion condition.
    (void)esp_wifi_clear_ap_list();
    state.scan=err==ESP_OK?WifiScanState::Complete:WifiScanState::Failed;
    state.scanError=err; ++state.scanRevision;
    if(targetScan) {
        const auto now=esp_timer_get_time();
        ESP_LOGI("wifi","probe done attempt=%u mode=%s result=%d found=%d records=%u scan_ms=%lld elapsed_ms=%lld",
            static_cast<unsigned>(state.retry)+1,broadcastProbe?"broadcast":"directed",err,int(err==ESP_OK && found),
            static_cast<unsigned>(count),elapsedMs(probeStarted,now),elapsedMs(attemptStarted,now));
        probeStarted=-1;
        targetScan=false;
        if(err==ESP_OK && found) { targetValidated=true; targetSeenAt=networksSeenAt; }
        else if(err==ESP_OK && !broadcastProbe) broadcastProbe=true;
        else failed(err==ESP_OK?Connectivity::NetworkNotSeen:err);
    }
}
}
esp_err_t Connectivity::init() {
    if(hotspotOwner.load()) return HotspotBusy;
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(initialized) return ESP_OK;
    // A previous rollback may have failed; finish it before allocating again.
    if(driverInitialized || station || wifiHandler || ipHandler || networks || viewNetworks) {
        const auto err=releaseStoppedLocked();
        if(err!=ESP_OK) { reportStartFailure("pending cleanup",err);setState(WifiState::Failed,err);return err; }
    }
    if(!networks) networks=static_cast<wifi_ap_record_t *>(heap_caps_calloc(kMaxNetworks,sizeof(wifi_ap_record_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!viewNetworks) viewNetworks=static_cast<wifi_ap_record_t *>(heap_caps_calloc(kMaxNetworks,sizeof(wifi_ap_record_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!networks || !viewNetworks) {
        return startFailed("scan buffers",ESP_ERR_NO_MEM);
    }
    if(!events) events=xQueueCreateStatic(16,sizeof(RadioEvent),eventStorage,&eventControl);
    esp_err_t err=events?ESP_OK:ESP_ERR_NO_MEM;
    if(err==ESP_OK && !driverInitialized) {
        wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
        err=esp_wifi_init(&config);driverInitialized=err==ESP_OK;
    }
    if(err==ESP_OK) {
        // The convenience create_default helper asserts on allocation failure.
        // Keep low-memory failures recoverable, just like esp_wifi_init errors.
        const esp_netif_config_t config=ESP_NETIF_DEFAULT_WIFI_STA();
        station=esp_netif_new(&config);
        err=station?ESP_OK:ESP_ERR_NO_MEM;
        if(err==ESP_OK) err=esp_netif_attach_wifi_station(station);
        if(err==ESP_OK) err=esp_wifi_set_default_wifi_sta_handlers();
    }
    if(err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_STA);
    if(err==ESP_OK) err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,radioEvent,nullptr,&wifiHandler);
    if(err==ESP_OK) err=esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,radioEvent,nullptr,&ipHandler);
    if(err!=ESP_OK) {
        return startFailed("initialization",err);
    }
    initialized=true;return ESP_OK;
}
esp_err_t Connectivity::validateCredentials(const std::string &ssid,const std::string &password) {
    if(ssid.empty() || ssid.size()>32 || ssid.find('\0')!=std::string::npos || password.find('\0')!=std::string::npos) return ESP_ERR_INVALID_ARG;
    if(password.empty()) return ESP_OK;
    if(password.size()>=8 && password.size()<=63) return ESP_OK;
    if(password.size()==64 && std::all_of(password.begin(),password.end(),[](unsigned char c){return std::isxdigit(c)!=0;})) return ESP_OK;
    return ESP_ERR_INVALID_ARG;
}
esp_err_t Connectivity::setWifiEnabled(bool enabled) {
    if(hotspotOwner.load() && hotspotShared)return HotspotBusy;
    if(enabled) { const auto err=init();if(err!=ESP_OK) return err; }
    {
        const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
        if(!initialized) {
            const auto err=releaseStoppedLocked();
            if(err!=ESP_OK) { reportStartFailure("disabled cleanup",err);setState(WifiState::Failed,err);return err; }
            setState(WifiState::Off);return ESP_OK;
        }
        if(enabled==state.enabled && state.state!=WifiState::Failed && (enabled || awaitingStop)) return ESP_OK;
        transitionTimedOut=false;
        reconnect=false;targetSsid.clear();targetPassword.clear();
        retryAt=0; connectDeadline=0; connectQueued=false; targetValidated=false;
        broadcastProbe=false; networksSeenAt=targetSeenAt=-1; targetChannel=0; targetRssi=0;
        probeRetryAt=probeBusyDeadline=0;
        resetConnectionTiming();
        if(!enabled) {
            reconnect=false; restartPending=false; awaitingStart=false; startDelivered.store(false); stopScanLocked();
            (void)esp_wifi_disconnect();
            // Do not repeatedly stop a stopped driver: ESP_OK is not a promise
            // of another STA_STOP event. Preserve any outstanding stop fence.
            const bool needsStop=state.enabled || driverStarted.load();
            const auto err=awaitingStop || !needsStop?ESP_OK:esp_wifi_stop();
            if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_STARTED) { setState(WifiState::Failed,err); return err; }
            if(needsStop || awaitingStop) { awaitingStop=err!=ESP_ERR_WIFI_NOT_STARTED; transitionDeadline=awaitingStop?esp_timer_get_time()+4000000:0; }
            if(err==ESP_ERR_WIFI_NOT_STARTED) driverStarted.store(false);
            state.enabled=false; state.ssid.clear(); state.ip.clear(); state.rssi=0; state.retry=0; networkCount=0;
            Settings::setWifiOn(false); setState(WifiState::Off);
            if(!awaitingStop) {
                const auto cleanup=releaseStoppedLocked();
                if(cleanup!=ESP_OK) { reportStartFailure("disabled cleanup",cleanup);setState(WifiState::Failed,cleanup);return cleanup; }
            }
            return ESP_OK;
        }
        if(awaitingStop) {
            // Off->On is a requested state, not permission to reuse old callbacks.
            restartPending=true; state.enabled=true; Settings::setWifiOn(true);
            setState(WifiState::Disconnected); return ESP_OK;
        }
        // Failed association does not mean the driver stopped. Reuse an
        // observed START instead of waiting for an event start() may not repeat.
        const bool alreadyStarted=driverStarted.load();
        auto err=alreadyStarted?ESP_OK:esp_wifi_start();
        if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_STOPPED) return startFailed("start",err);
        awaitingStart=!alreadyStarted && err==ESP_OK; transitionDeadline=awaitingStart?esp_timer_get_time()+4000000:0;
        state.enabled=true; tuneRadio(); Settings::setWifiOn(true); setState(WifiState::Disconnected);
    }
    return ESP_OK; // switch enables only the radio; selecting a network connects
}
esp_err_t Connectivity::stopWifi() { return setWifiEnabled(false); }
esp_err_t Connectivity::disconnectWifi() {
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(!initialized) return ESP_OK;
    reconnect=false; connectQueued=false; targetValidated=false; retryAt=0; connectDeadline=0;
    broadcastProbe=false; networksSeenAt=targetSeenAt=-1; targetChannel=0; targetRssi=0;
    probeRetryAt=probeBusyDeadline=0;
    resetConnectionTiming();
    stopScanLocked();
    const auto err=requestDisconnectLocked();
    if(err!=ESP_OK) return err;
    state.ip.clear(); state.rssi=0; state.retry=0;
    setState(state.enabled?WifiState::Disconnected:WifiState::Off);
    return ESP_OK;
}
esp_err_t Connectivity::connectWifi(const std::string &ssid,const std::string &password) {
    if(validateCredentials(ssid,password)!=ESP_OK) return ESP_ERR_INVALID_ARG;
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(!state.enabled) return ESP_ERR_INVALID_STATE;
    if(state.scan==WifiScanState::Scanning) stopScanLocked();
    const auto err=requestDisconnectLocked();
    if(err!=ESP_OK) {
        reconnect=false; retryAt=0; failed(err);
        return err; // never scan on top of a driver which refused the disconnect
    }
    targetSsid=ssid; targetPassword=password; reconnect=true; retryAt=0; state.retry=0;
    resetConnectionTiming(esp_timer_get_time());
    probeRetryAt=probeBusyDeadline=0; probeRecovered=false;
    targetValidated=false; connectQueued=true; connectDeadline=0;
    broadcastProbe=false; targetSeenAt=-1; targetChannel=0; targetRssi=0;
    validateCachedTarget();
    ESP_LOGI("wifi","connect requested attempt=1 cached=%d channel=%u",int(targetValidated),static_cast<unsigned>(targetChannel));
    state.ssid=ssid; state.ip.clear(); state.rssi=0;
    setState(WifiState::Searching);
    return ESP_OK; // poll waits for the cancelled scan / STA_STOP / STA_START fences.
}
esp_err_t Connectivity::startScan() {
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(!state.enabled || awaitingStart || ignoreDisconnect || state.state==WifiState::Searching || state.state==WifiState::Connecting) return ESP_ERR_INVALID_STATE;
    if(state.scan==WifiScanState::Scanning) return ESP_OK;
    if(scanOutstanding || awaitingStop) return ESP_ERR_INVALID_STATE;
    (void)esp_wifi_clear_ap_list();
    wifi_scan_config_t config{}; config.show_hidden=false;
    auto err=esp_wifi_scan_start(&config,false);
    scanOutstanding=err==ESP_OK;
    state.scan=err==ESP_OK?WifiScanState::Scanning:WifiScanState::Failed;
    state.scanError=err; ++state.scanRevision;
    scanDeadline=err==ESP_OK?esp_timer_get_time()+kScanTimeout:0;
    return err;
}
void Connectivity::cancelScan() { const std::lock_guard<std::mutex> lock(mutex); PublishView publish; stopScanLocked(); }
void Connectivity::cancelUserScan() {
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(initialized && !targetScan && !connectQueued && state.scan==WifiScanState::Scanning) stopScanLocked();
}
void Connectivity::poll() {
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(!initialized) return;
    if(state.enabled && driverStarted.load() && !hotspotOwner.load())tunePowerSave();
    const int scanStatus=scanCompletion.exchange(-1);
    if(scanStatus>=0 && scanOutstanding) {
        scanOutstanding=false; cancelDeadline=0;
        consumeScan(scanStatus);
    }
    if(stopDelivered.exchange(false)) {
        // Default event-loop callbacks are serialized. No new scan can start
        // before this fence, even when Off/On requests were queued together.
        awaitingStop=false; transitionDeadline=0; transitionTimedOut=false; cancelDeadline=0;
        ignoreDisconnect=false; connectionActive=false; disconnectDeadline=0;
        scanOutstanding=false;
        scanCompletion.store(-1);
        if(!state.enabled && !restartPending) {
            const auto err=releaseStoppedLocked();
            if(err!=ESP_OK) { reportStartFailure("stopped cleanup",err);setState(WifiState::Failed,err); }
            return;
        }
    }
    if(startDelivered.exchange(false) && !awaitingStop) {
        awaitingStart=false; transitionDeadline=0; transitionTimedOut=false;
        // A late START can finish recovery; a saved target still requires a probe.
        if(state.enabled && state.state==WifiState::Failed && reconnect && !retryAt) {
            connectQueued=true; targetValidated=false; setState(WifiState::Searching);
        }
    }
    if(restartPending && !awaitingStop) {
        restartPending=false;
        const auto err=esp_wifi_start();
        if(err==ESP_OK || err==ESP_ERR_WIFI_NOT_STOPPED) {
            awaitingStart=err==ESP_OK; transitionDeadline=awaitingStart?esp_timer_get_time()+4000000:0; tuneRadio();
            reconnect=reconnect && validateCredentials(targetSsid,targetPassword)==ESP_OK;
            connectQueued=reconnect && !retryAt; targetValidated=false;
            state.ssid=targetSsid;
            if(connectQueued) setState(WifiState::Searching);
            else if(state.state!=WifiState::Failed) setState(WifiState::Disconnected);
        } else { (void)startFailed("restart",err);return; }
    }
    RadioEvent event{};
    for(unsigned i=0;i<16 && xQueueReceive(events,&event,0)==pdTRUE;++i) {
        if(!state.enabled || awaitingStop || awaitingStart) continue;
        if(event.kind==RadioEvent::Disconnected) {
            // Finish the old operation before starting any new probe/connection.
            // A manual retry must not clear this fence or consume another retry.
            if(ignoreDisconnect) {
                ignoreDisconnect=false; connectionActive=false; disconnectDeadline=0;
                continue;
            }
            // Intentional disconnect from the previous generation cannot fail a probe.
            if(state.state==WifiState::Searching) continue;
            if(event.ssid[0] && !targetSsid.empty() && targetSsid!=event.ssid) continue;
            if(!connectionActive) continue; // no current association owns a duplicate terminal event
            connectionActive=false;
            if(reconnect) failed(event.detail);
            else { state.ip.clear(); setState(WifiState::Disconnected); }
        } else if(event.kind==RadioEvent::Associated) {
            // A late/duplicate notification cannot extend a failed, cancelled,
            // replaced or already-addressing attempt. Verify the live driver.
            if(ignoreDisconnect || !reconnect || !connectionActive || state.state!=WifiState::Connecting || state.phase!=WifiConnectPhase::Joining) continue;
            if(event.ssid[0] && targetSsid!=event.ssid) continue;
            wifi_ap_record_t ap{};
            if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK || !sameSsid(ap.ssid,targetSsid)) continue;
            state.phase=WifiConnectPhase::Address; state.rssi=ap.rssi; ++state.revision;
            addressStarted=esp_timer_get_time();
            // join_ms includes the SDK's internal scan and authentication.
            ESP_LOGI("wifi","associated attempt=%u join_ms=%lld elapsed_ms=%lld rssi=%d",
                static_cast<unsigned>(state.retry)+1,elapsedMs(joinStarted,addressStarted),elapsedMs(attemptStarted,addressStarted),state.rssi);
            connectDeadline=addressStarted+kAddressTimeout;
        } else if(event.kind==RadioEvent::GotIp) {
            if(ignoreDisconnect || !reconnect || !connectionActive || state.state!=WifiState::Connecting) continue; // reject stale DHCP completion
            wifi_ap_record_t ap{}; esp_netif_ip_info_t info{};
            auto netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK || !netif || esp_netif_get_ip_info(netif,&info)!=ESP_OK || !info.ip.addr) continue;
            if(!targetSsid.empty() && targetSsid!=std::string((char *)ap.ssid,strnlen((char *)ap.ssid,32))) continue;
            char ip[16]; esp_ip4addr_ntoa(&info.ip,ip,sizeof(ip));
            state.ip=ip; state.ssid.assign((char *)ap.ssid,strnlen((char *)ap.ssid,32)); state.rssi=ap.rssi;
            const auto now=esp_timer_get_time();
            ESP_LOGI("wifi","online attempt=%u dhcp_ms=%lld total_ms=%lld rssi=%d",
                static_cast<unsigned>(state.retry)+1,elapsedMs(addressStarted,now),elapsedMs(attemptStarted,now),state.rssi);
            resetConnectionTiming();
            state.retry=0; retryAt=0; connectDeadline=0; ignoreDisconnect=false;
            Settings::setWifiCredentials(targetSsid,targetPassword);
            Settings::rememberWifi(targetSsid,targetPassword); // only after DHCP success
            setState(WifiState::Connected);
        } else if(event.kind==RadioEvent::LostIp && state.state==WifiState::Connected) {
            const auto err=requestDisconnectLocked(); failed(err==ESP_OK?ESP_ERR_TIMEOUT:err);
        }
    }
    const auto now=esp_timer_get_time();
    if(transitionDeadline && now>=transitionDeadline) {
        transitionDeadline=0; transitionTimedOut=true;
        connectQueued=false; // retain any association backoff through a late START
        setState(WifiState::Failed,ESP_ERR_TIMEOUT); // keep fence; allow user toggle/recovery
    }
    if(((cancelDeadline && now>=cancelDeadline && scanOutstanding) ||
        (disconnectDeadline && now>=disconnectDeadline && ignoreDisconnect)) && !awaitingStop) {
        cancelDeadline=0;
        disconnectDeadline=0;
        // Missing scan/disconnect completion cannot leave the UI stuck forever.
        // STA_STOP is the recovery fence; keep credentials and requested switch.
        const auto err=restartStationLocked(now);
        if(err!=ESP_OK) {
            transitionTimedOut=true; connectQueued=false; retryAt=0;
            setState(WifiState::Failed,err);
        }
    }
    if(overflow.exchange(false)) {
        if(state.scan==WifiScanState::Scanning) scanDeadline=now;
        if(state.state==WifiState::Connecting) connectDeadline=now;
    }
    if(scanDeadline && now>=scanDeadline) {
        const bool wasTarget=targetScan;
        stopScanLocked(); state.scan=WifiScanState::Failed; state.scanError=ESP_ERR_TIMEOUT; ++state.scanRevision;
        if(wasTarget) failed(ESP_ERR_TIMEOUT);
    }
    if(connectDeadline && now>=connectDeadline) {
        const auto err=requestDisconnectLocked(); failed(err==ESP_OK?ESP_ERR_TIMEOUT:err);
    }
    if(retryAt && now>=retryAt && state.enabled) {
        retryAt=0; connectQueued=true; targetValidated=false; setState(WifiState::Searching);
        broadcastProbe=false; targetSeenAt=-1; targetChannel=0; targetRssi=0;
        resetConnectionTiming(now);
        ESP_LOGI("wifi","connect retry attempt=%u",static_cast<unsigned>(state.retry)+1);
    }
    if(connectQueued && state.enabled && !awaitingStop && !awaitingStart && !scanOutstanding && !ignoreDisconnect && now>=probeRetryAt) {
        if(targetValidated && !recentPresence(targetSeenAt)) { targetValidated=false; targetChannel=0; targetRssi=0; }
        if(targetValidated) {
            connectQueued=false; targetValidated=false;
            const auto err=beginConnect(); if(err!=ESP_OK) failed(err);
        } else {
            const auto err=beginTargetScan();
            if(err==ESP_ERR_WIFI_STATE) {
                if(!probeBusyDeadline) probeBusyDeadline=now+kDriverBusyTimeout;
                if(now<probeBusyDeadline) probeRetryAt=now+kProbeRetryInterval;
                else if(!probeRecovered) {
                    probeRecovered=true;
                    const auto recovery=restartStationLocked(now);
                    if(recovery!=ESP_OK) failed(recovery);
                } else failed(err); // one driver recovery per explicit connection; no infinite restarts
            } else {
                probeRetryAt=probeBusyDeadline=0;
                if(err!=ESP_OK) failed(err);
            }
        }
    }
    static int64_t nextSignal=0;
    if(state.state==WifiState::Connected && now>=nextSignal) {
        nextSignal=now+2000000; wifi_ap_record_t ap{};
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK) state.rssi=ap.rssi;
        else failed(ESP_ERR_WIFI_NOT_CONNECT);
    }
}

esp_err_t Connectivity::acquireForHotspot() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if(hotspotShared)return ESP_OK;
        // Starting/connecting STA may obtain an address after the local AP is
        // ready. Keep its normal bounded retries; never copy its credentials.
        if(!hotspotOwner.load() && state.enabled && initialized && station) {
            if(awaitingStart || awaitingStop || restartPending || ignoreDisconnect || scanOutstanding)
                return transitionTimedOut?ESP_ERR_TIMEOUT:ESP_ERR_NOT_FINISHED;
            if(driverStarted.load()) {hotspotShared=true;hotspotOwner=true;return ESP_OK;}
        }
    }
    // With STA disabled, retain the existing stop fence and AP-only ownership.
    if(!hotspotOwner.exchange(true)) {
        const auto err=setWifiEnabled(false);
        if(err!=ESP_OK) return err;
    }
    const std::lock_guard<std::mutex> lock(mutex); PublishView publish;
    if(awaitingStop) return transitionTimedOut?ESP_ERR_TIMEOUT:ESP_ERR_NOT_FINISHED;
    return releaseStoppedLocked();
}
void Connectivity::releaseFromHotspot() {
    const std::lock_guard<std::mutex> lock(mutex);
    hotspotShared=false;hotspotOwner=false;appliedPowerSave=-1;
}
bool Connectivity::hotspotReserved() { return hotspotOwner.load(); }
bool Connectivity::hotspotUsesStation() {
    const std::lock_guard<std::mutex> lock(mutex);return hotspotShared;
}
HotspotUplink Connectivity::hotspotUplink() {
    const std::lock_guard<std::mutex> lock(mutex);
    HotspotUplink out;
    if(!hotspotShared || !station || !state.enabled || state.state!=WifiState::Connected || !driverStarted.load())return out;
    esp_netif_ip_info_t info{};wifi_ap_record_t associated{};
    if(esp_wifi_sta_get_ap_info(&associated)!=ESP_OK || esp_netif_get_ip_info(station,&info)!=ESP_OK || !info.ip.addr || !info.gw.addr)return out;
    out.online=true;out.ip=info.ip.addr;out.netmask=info.netmask.addr;out.gateway=info.gw.addr;
    esp_netif_dns_info_t dns{};
    if(esp_netif_get_dns_info(station,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK && dns.ip.type==ESP_IPADDR_TYPE_V4)out.dns=dns.ip.u_addr.ip4.addr;
    if(!out.dns && esp_netif_get_dns_info(station,ESP_NETIF_DNS_BACKUP,&dns)==ESP_OK && dns.ip.type==ESP_IPADDR_TYPE_V4)out.dns=dns.ip.u_addr.ip4.addr;
    return out;
}
esp_err_t Connectivity::useStationRoute() {
    const std::lock_guard<std::mutex> lock(mutex);
    return hotspotShared && station?esp_netif_set_default_netif(station):ESP_ERR_INVALID_STATE;
}

WifiSnapshot Connectivity::snapshot() { const std::lock_guard<std::mutex> lock(viewMutex); return view; }
uint16_t Connectivity::scanResults(wifi_ap_record_t *out,uint16_t capacity) {
    if(!out) return 0;
    const std::lock_guard<std::mutex> lock(viewMutex);
    auto n=std::min(capacity,viewCount); if(n) std::copy_n(viewNetworks,n,out); return n;
}
bool Connectivity::wifiEnabled() { const std::lock_guard<std::mutex> lock(viewMutex); return view.enabled; }
WifiState Connectivity::wifiState() { const std::lock_guard<std::mutex> lock(viewMutex); return view.state; }
std::string Connectivity::wifiSsid() { const std::lock_guard<std::mutex> lock(viewMutex); return view.ssid; }
int Connectivity::wifiRssi() { const std::lock_guard<std::mutex> lock(viewMutex); return view.rssi; }
} // namespace sys
