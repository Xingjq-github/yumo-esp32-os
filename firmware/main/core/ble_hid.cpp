#include "ble_hid.hpp"
#include "hid_key_sequence.hpp"
#include "hid_mouse_queue.hpp"
#include "battery_mgr.hpp"
#include "power_mgr.hpp"
#include "ble_keyboard_client.hpp"
#include "ble_peer_store.hpp"
#include "ble_peer_identity.hpp"
#include "keyboard_input.hpp"
#include "host/ble_gatt.h"
#include "host/ble_esp_gap.h"
#include <algorithm>

#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "esp_bt.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "yumo_ble_store.h"
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
#include <new>

// The SDK's automatic reattempt path may delete a failed connection without
// delivering DISCONNECT, then restart advertising outside our manual window.
// Keep one owner for connection lifetime, cancellation and the single slot.
#if defined(CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT) && CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT
#error "BLE automatic reattempt must be disabled: manual connection windows own retries"
#endif

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
constexpr int kMaxBondSlots = BlePeerStore::Capacity;
#endif
static_assert(kMaxBondSlots<=BlePeerStore::Capacity,"BLE peer metadata must cover every saved bond");

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
// Host-event-queue-owned timer: let a Central restore encryption / discover
// services before sending our one fallback Security Request. No worker sleep.
ble_npl_callout s_securityTimer{};
bool s_securityTimerInitialized=false;
uintptr_t s_securityLink=0;
uint16_t s_securityHandle=kInvalidHandle;
bool s_securityRequested=false;
uintptr_t s_secureCompletedLink=0;
// Advertising may already have stopped in the controller while CONNECT is
// still waiting for remote features. Do not reuse that occupied slot.
std::atomic<bool> s_advAwaitingResult{false},s_gapTeardown{false};
std::atomic<uintptr_t> s_advGeneration{0};
void processAdvertising();
// Only a manual, bounded connection window permits advertising/connecting.
// A bond is history, never permission to reconnect after either peer disconnects.
std::atomic<bool> s_advertisingAllowed{false}, s_connectionAllowed{false}, s_targeted{false};
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
std::atomic<BleScanState> s_scanState{BleScanState::Idle};
// Task-side only; recursive because setEnabled() calls init()/stopScan().
// Keep scan commands out of host teardown. Callbacks never take this mutex.
std::recursive_mutex s_scanControlMutex;
portMUX_TYPE s_scanLock = portMUX_INITIALIZER_UNLOCKED;
std::uintptr_t s_scanGeneration = 0; // Protected by s_scanLock; never reused.
// Host/worker-only scan snapshots, not DMA/ISR data. The existing XIP policy
// keeps PSRAM accessible during NVS writes; locks/atomics/TCB stay internal.
BleScanDevice *s_scanDevices=nullptr;
int s_scanCount = 0;
std::atomic<bool> s_keyboardLink{false},s_connectPending{false};
std::atomic<bool> s_namePending{false};
std::atomic<uintptr_t> s_linkGeneration{0};
std::atomic<bool> s_privateEligible{false},s_requireAuthenticated{false};
// Host-callback-owned copy, retained through encryption even after the manual
// advertisement is consumed. Worker-side selection cannot relabel this link.
BleBond s_linkExpected{};
bool s_verifyExpected=false;
std::atomic<uint32_t> s_privatePermit{1};
// Static transport state, never an App/Job pointer. Late host events cannot
// dereference a destroyed app or send a password to a replacement connection.
struct PrivateStroke {
    ble_npl_callout timer{};bool initialized=false,releasing=false;
    std::atomic<bool> busy{false},done{true};std::atomic<int> result{ESP_OK};
    uintptr_t link=0;uint32_t permit=0;uint8_t key=0,modifiers=0;
} s_privateStroke;
esp_err_t sendReport(uint8_t reportId,uint8_t *data,size_t length);
void privateStrokeEvent(ble_npl_event *) {
    auto &s=s_privateStroke;
    const bool same=s.link==s_linkGeneration.load()&&BleHid::remoteConnected()&&s_privateEligible.load();
    if(s.releasing){
        uint8_t zero[8]{};if(same){auto err=sendReport(kKeyboardReportId,zero,sizeof(zero));if(err!=ESP_OK)s.result.store(err);}
        else s.result.store(ESP_ERR_INVALID_STATE);
    }else if(same&&s.permit==s_privatePermit.load()&&Power::isScreenOn()&&!Power::sleepPending()){
        uint8_t pressed[8]={s.modifiers,0,s.key,0,0,0,0,0};
        s.result.store(sendReport(kKeyboardReportId,pressed,sizeof(pressed)));std::memset(pressed,0,sizeof(pressed));
        s.releasing=true;
        if(ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(40))==0)return;
        uint8_t zero[8]{};if(same)(void)sendReport(kKeyboardReportId,zero,sizeof(zero));s.result.store(ESP_FAIL);
    }else s.result.store(ESP_ERR_INVALID_STATE);
    s.key=0;s.modifiers=0;s.busy.store(false);s.done.store(true);
}
// Ordinary keyboard output is queued without blocking LVGL. Only the NimBLE
// host advances reports; GAP generation changes cannot split a key across peers.
// Fixed queue storage, no worker task, no App pointers or queued heap objects.
struct KeyboardOutput {
    ble_npl_callout timer{};bool initialized=false;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> epoch{1};
    std::atomic<int> error{ESP_OK};
    std::atomic<uintptr_t> blockedLink{0}; // fail closed until a new connection
    hid::KeyQueue queue{}; // s_keyQueueLock
    hid::QueuedKey current{};hid::KeySequence sequence{};bool active=false; // host only
} s_keys;
portMUX_TYPE s_keyQueueLock=portMUX_INITIALIZER_UNLOCKED;
// Producers only. Host callbacks never take this mutex. Also serializes channel
// ownership with the private/password transport, whose authentication is intact.
std::mutex s_keySubmitMutex;
bool currentKeyLink() {
    return s_keys.current.link==s_linkGeneration.load() && BleHid::remoteConnected();
}
void finishKeyQueue() {
    s_keys.current={};s_keys.sequence={};s_keys.active=false;
    portENTER_CRITICAL(&s_keyQueueLock);
    s_keys.queue.clear();s_keys.running.store(false);
    portEXIT_CRITICAL(&s_keyQueueLock);
}
void abortKeyLink() {
    // If an all-up report repeatedly fails, disconnect this link rather than
    // keep a host's Command/Ctrl held. Never terminate a replacement peer.
    s_keys.blockedLink.store(s_keys.current.link);
    if(currentKeyLink())(void)ble_gap_terminate(s_connHandle.load(),BLE_ERR_REM_USER_CONN_TERM);
    finishKeyQueue();
}
void keyStrokeEvent(ble_npl_event *) {
    auto &s=s_keys;
    if(s.active && (!currentKeyLink() || s.sequence.phase==hid::KeySequence::Phase::Done)) {
        s.current={};s.sequence={};s.active=false;
    }
    while(!s.active) {
        hid::QueuedKey key{};
        portENTER_CRITICAL(&s_keyQueueLock);
        if(!s.queue.pop(key)) {
            s.running.store(false);portEXIT_CRITICAL(&s_keyQueueLock);return;
        }
        portEXIT_CRITICAL(&s_keyQueueLock);
        if(key.link!=s_linkGeneration.load() || key.epoch!=s.epoch.load() ||
           !BleHid::remoteConnected() || !Power::isScreenOn() || Power::sleepPending())continue;
        s.current=key;s.sequence.start(key.key,key.modifiers);s.active=true;
    }
    if(s.current.epoch!=s.epoch.load() || !Power::isScreenOn() || Power::sleepPending())s.sequence.cancel();
    auto report=s.sequence.report();
    const auto delay=s.sequence.delayMs();
    const auto err=sendReport(kKeyboardReportId,report.data(),report.size());
    if(err!=ESP_OK) { s.error.store(err);++s.epoch; } // discard pending presses, retry only all-up
    s.sequence.sent(err==ESP_OK);
    if(s.sequence.phase==hid::KeySequence::Phase::Abort) { abortKeyLink();return; }
    if(ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(delay))!=0) {
        s.error.store(ESP_FAIL);++s.epoch;
        uint8_t zero[8]{};
        if(currentKeyLink() && sendReport(kKeyboardReportId,zero,sizeof(zero))!=ESP_OK)abortKeyLink();
        else finishKeyQueue();
    }
}
std::atomic<int64_t> s_connectDeadline{0};
// Mouse producers do no GATT work. The host owns report transmission and GAP
// generation changes; the small critical sections only move fixed-size state.
struct MouseOutput {
    ble_npl_callout timer{};std::atomic<bool> initialized{false};
    std::atomic<bool> running{false};
    std::atomic<int> error{ESP_OK};
    std::atomic<int64_t> nextSendAt{0};
    hid::MouseQueue queue{}; // s_mouseLock
    uintptr_t link=0;bool scheduled=false; // s_mouseLock
    int64_t failedSince=0,connectedAt=0;bool failureReported=false; // host only
    uintptr_t tunedLink=0,blockedLink=0; // host only except producer guarded read
    uint32_t sent=0,sendErrors=0,maxSendUs=0; // host-only, summarized at disconnect
} s_mouse;
portMUX_TYPE s_mouseLock=portMUX_INITIALIZER_UNLOCKED;
std::mutex s_mouseSubmitMutex; // producers only, never taken by host callbacks

void resetMouseOutput() {
    if(s_mouse.initialized)ble_npl_callout_stop(&s_mouse.timer);
    portENTER_CRITICAL(&s_mouseLock);
    s_mouse.queue.reset();s_mouse.link=0;s_mouse.scheduled=false;s_mouse.running.store(false);
    s_mouse.blockedLink=0;s_mouse.nextSendAt.store(0);
    portEXIT_CRITICAL(&s_mouseLock);
    s_mouse.error.store(ESP_OK);s_mouse.failedSince=0;s_mouse.failureReported=false;
    s_mouse.tunedLink=0;s_mouse.connectedAt=esp_timer_get_time();
    s_mouse.sent=s_mouse.sendErrors=s_mouse.maxSendUs=0;
}
void reportMouseSession() {
    if(!s_mouse.sent && !s_mouse.sendErrors)return;
    ESP_LOGI(kTag,"Mouse session sent=%lu errors=%lu max_send_us=%lu",
             static_cast<unsigned long>(s_mouse.sent),static_cast<unsigned long>(s_mouse.sendErrors),
             static_cast<unsigned long>(s_mouse.maxSendUs));
}
unsigned mouseIntervalMs(uint16_t handle) {
    ble_gap_conn_desc desc{};
    if(ble_gap_conn_find(handle,&desc)!=0)return 15;
    const unsigned interval=(unsigned(desc.conn_itvl)*5+3)/4;
    // Never flood the ACL queue faster than the accepted connection interval.
    return std::max(12u,std::min(4000u,interval));
}
unsigned mouseScheduleDelayMs() {
    const auto wait=s_mouse.nextSendAt.load()-esp_timer_get_time();
    return wait>0?unsigned((wait+999)/1000):1;
}
void tuneMouseConnection(uintptr_t link,uint16_t handle) {
    if(s_mouse.tunedLink==link || esp_timer_get_time()-s_mouse.connectedAt<5000000)return;
    s_mouse.tunedLink=link;
    ble_gap_conn_desc desc{};
    if(ble_gap_conn_find(handle,&desc)!=0 || desc.role!=BLE_GAP_ROLE_SLAVE)return;
    if(desc.conn_itvl<=12 && !desc.conn_latency)return;
    ble_gap_upd_params params{};
    params.itvl_min=9;params.itvl_max=12; // HID: 11.25-15ms; peer has final choice
    params.latency=0;params.supervision_timeout=600;
    const int rc=ble_gap_update_params(handle,&params);
    ESP_LOGI(kTag,"Mouse connection interval request rc=%d current_units=%u latency=%u",rc,desc.conn_itvl,desc.conn_latency);
    // A host may reject/override the preference. Never disconnect or retry
    // repeatedly; subsequent reports simply use its actual interval.
}
void mouseScheduleFailure(uintptr_t link) {
    bool held=false;
    portENTER_CRITICAL(&s_mouseLock);
    if(s_mouse.link==link){s_mouse.queue.cancel();held=s_mouse.queue.held();}
    portEXIT_CRITICAL(&s_mouseLock);
    if(held && link==s_linkGeneration.load() && BleHid::remoteConnected()) {
        uint8_t zero[4]{};
        if(sendReport(kMouseReportId,zero,sizeof(zero))!=ESP_OK)
            (void)ble_gap_terminate(s_connHandle.load(),BLE_ERR_REM_USER_CONN_TERM);
    }
    portENTER_CRITICAL(&s_mouseLock);
    if(s_mouse.link==link){s_mouse.queue.reset();s_mouse.scheduled=false;s_mouse.running.store(false);}
    portEXIT_CRITICAL(&s_mouseLock);
    s_mouse.error.store(ESP_FAIL);
}
void mouseOutputEvent(ble_npl_event *) {
    auto &s=s_mouse;
    const auto link=s_linkGeneration.load();
    const auto handle=s_connHandle.load();
    portENTER_CRITICAL(&s_mouseLock);
    if(s.link!=link || !BleHid::remoteConnected()) {
        s.queue.reset();s.link=0;s.scheduled=false;s.running.store(false);
        portEXIT_CRITICAL(&s_mouseLock);s.failedSince=0;return;
    }
    if(Power::sleepPending())s.queue.cancel();
    if(s.queue.pending() && esp_timer_get_time()<s.nextSendAt.load()) {
        // The queue may have drained between touch samples. Retain the last
        // transmission deadline so new samples cannot restart a 1ms burst.
        portEXIT_CRITICAL(&s_mouseLock);
        if(ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(mouseScheduleDelayMs()))!=0)mouseScheduleFailure(link);
        return;
    }
    const auto ticket=s.queue.begin();
    if(!ticket.valid) {
        s.scheduled=false;s.running.store(!s.queue.idle());
        portEXIT_CRITICAL(&s_mouseLock);return;
    }
    portEXIT_CRITICAL(&s_mouseLock);
    auto report=ticket.bytes;
    const auto sendStarted=esp_timer_get_time();
    const auto err=sendReport(kMouseReportId,report.data(),report.size());
    const auto now=esp_timer_get_time();
    s.maxSendUs=std::max(s.maxSendUs,static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,now-sendStarted)));
    if(err==ESP_OK)++s.sent;else ++s.sendErrors;
    const unsigned delay=std::max(mouseIntervalMs(handle),err==ESP_OK?ticket.dwellMs:50u);
    bool fatalHeld=false;
    portENTER_CRITICAL(&s_mouseLock);
    if(s.link!=link || link!=s_linkGeneration.load()) {
        // A stopped/replaced link never consumes or releases another peer's
        // work, even if a transport shim synchronously changes generation.
        portEXIT_CRITICAL(&s_mouseLock);return;
    }
    s.nextSendAt.store(now+int64_t(delay)*1000);
    s.queue.complete(ticket,err==ESP_OK);
    if(err==ESP_OK){s.failedSince=0;s.failureReported=false;if(s.queue.idle())s.blockedLink=0;}
    else {
        if(!s.failedSince)s.failedSince=now;
        const bool permanent=err==ESP_ERR_INVALID_ARG || err==ESP_ERR_INVALID_STATE ||
            err==ESP_ERR_NOT_FOUND || err==ESP_ERR_NOT_SUPPORTED || err==ESP_ERR_INVALID_SIZE;
        if(permanent || now-s.failedSince>=500000) {
            // A stale gesture must not catch up after a prolonged radio stall.
            // A previously accepted button is still released before new work.
            s.queue.cancel();
            if(!s.failureReported){s.error.store(err);s.failureReported=true;}
        }
        if((permanent || now-s.failedSince>=4000000) && s.queue.held() && s.blockedLink!=link) {
            s.blockedLink=link;fatalHeld=true;
        }
    }
    const bool pending=s.queue.pending();
    s.scheduled=pending;s.running.store(!s.queue.idle());
    if(!pending&&!s.queue.held()){s.failedSince=0;s.failureReported=false;}
    portEXIT_CRITICAL(&s_mouseLock);
    if(fatalHeld) {
        // Only four seconds of continuously failed all-up on this exact link
        // warrants teardown, preventing a permanently held host button.
        ESP_LOGW(kTag,"Mouse all-up unavailable; closing stalled link rc=%d stalled_ms=%ld",err,long((now-s.failedSince)/1000));
        if(link==s_linkGeneration.load() && BleHid::remoteConnected())
            (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
    }
    tuneMouseConnection(link,handle);
    if(pending) {
        if(ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(delay))!=0)mouseScheduleFailure(link);
    }
}
void recoverMouseSchedule() {
    if(!s_mouse.running.load() || !s_mouse.initialized.load() || !s_hostRunning.load())return;
    std::unique_lock<std::mutex> producer(s_mouseSubmitMutex,std::try_to_lock);
    if(!producer.owns_lock())return;
    portENTER_CRITICAL(&s_mouseLock);
    const bool start=s_mouse.queue.pending()&&!s_mouse.scheduled;
    if(start)s_mouse.scheduled=true;
    portEXIT_CRITICAL(&s_mouseLock);
    if(start && ble_npl_callout_reset(&s_mouse.timer,ble_npl_time_ms_to_ticks32(mouseScheduleDelayMs()))!=0) {
        portENTER_CRITICAL(&s_mouseLock);s_mouse.scheduled=false;portEXIT_CRITICAL(&s_mouseLock);
        s_mouse.error.store(ESP_FAIL);
    }
}
std::atomic<bool> s_presenceScan{false}; // one explicit scan-before-connect, never periodic
BleBond s_presenceTarget{};
int s_lastBattery=-1;int64_t s_nextBattery=0,s_nextPeerFlush=0;
int nameRead(uint16_t handle,const ble_gatt_error *error,ble_gatt_attr *attr,void *arg) {
    if(handle!=s_connHandle.load() || reinterpret_cast<uintptr_t>(arg)!=s_linkGeneration.load()) return 0;
    if(!error->status && attr) {
        char name[64]{};const auto n=std::min<size_t>(OS_MBUF_PKTLEN(attr->om),sizeof(name)-1);
        ble_hs_mbuf_to_flat(attr->om,name,n,nullptr);
        // Keep only complete UTF-8 code points and printable bytes.
        size_t end=0;
        while(end<n) {
            auto c=static_cast<uint8_t>(name[end]);
            const size_t width=c<0x80?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
            if(!width) { name[end++]='?';continue; }
            if(end+width>n) break;
            bool valid=true;for(size_t j=1;j<width;++j) valid&=(static_cast<uint8_t>(name[end+j])&0xc0)==0x80;
            if(!valid) { name[end++]='?';continue; }
            if(c<0x20 || c==0x7f) name[end]=' ';
            end+=width;
        }
        name[end]=0;
        BlePeerStore::name(name);
    }
    return 0;
}
void publishBattery(int64_t now) {
    auto *device=s_device.load();
    if(s_keyboardLink.load() || !BleHid::remoteConnected() || !device || !esp_hidd_dev_connected(device)) { s_lastBattery=-1;return; }
    if(now<s_nextBattery) return;
    s_nextBattery=now+5000000;
    const int level=Battery::level();
    if(level<0 || level==s_lastBattery) return;
    const auto err=esp_hidd_dev_battery_set(device,static_cast<uint8_t>(level));
    if(err==ESP_OK) s_lastBattery=level;
    else ESP_LOGW(kTag,"Cannot publish battery level: %s",esp_err_to_name(err));
}

bool connectionPeer(const ble_gap_conn_desc &desc,BleBond &peer,bool allowUnresolved=false) {
    ble_addr_t identity{};
    // Some controller/host privacy paths initially expose the OTA RPA here.
    // Use the saved IRK resolver, just as scan-before-connect does. No new bond
    // is synthesized and an unresolvable address is never accepted as a target.
    if(!ble::resolveIdentity(desc.peer_id_addr,identity,ble_gap_rpa_resolve) &&
       !ble::resolveIdentity(desc.peer_ota_addr,identity,ble_gap_rpa_resolve)) {
        // A NEW phone has not shared its IRK until pairing. Discovery may
        // accept its raw address, but a targeted request still compares it
        // exactly below. Encryption must subsequently resolve the identity.
        if(!allowUnresolved || desc.peer_id_addr.type>3 ||
           !std::any_of(desc.peer_id_addr.val,desc.peer_id_addr.val+6,[](uint8_t v){return v!=0;}))return false;
        identity=desc.peer_id_addr;identity.type&=1;
    }
    peer.addrType=identity.type;std::memcpy(peer.addr,identity.val,6);
    peer=BlePeerStore::enrich(peer);peer.keyboard=desc.role==BLE_GAP_ROLE_MASTER;
    return true;
}
bool matchesLinkExpected(const BleBond &peer) {
    if(!s_verifyExpected)return true;
    auto expected=s_linkExpected;
    ble_addr_t address{},identity{};address.type=expected.addrType;std::memcpy(address.val,expected.addr,6);
    // A newly selected external keyboard may initially advertise with an RPA;
    // after SMP, its newly shared IRK can resolve that same selected address.
    if(ble::resolveIdentity(address,identity,ble_gap_rpa_resolve)) {
        expected.addrType=identity.type;std::memcpy(expected.addr,identity.val,6);
    }
    return BlePeerStore::same(expected,peer) && expected.keyboard==peer.keyboard;
}


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
    BleBond identity;identity.addrType=disc.addr.type;std::memcpy(identity.addr,disc.addr.val,6);
    bool cached=false;
    portENTER_CRITICAL(&s_scanLock);
    for(int i=0;i<s_scanCount;++i) if(s_scanDevices[i].address.addrType==disc.addr.type &&
       std::memcmp(s_scanDevices[i].address.addr,disc.addr.val,6)==0) { identity=s_scanDevices[i].identity;cached=true;break; }
    portEXIT_CRITICAL(&s_scanLock);
    if(!cached) {
        ble_addr_t resolved{};
        if(ble::resolveIdentity(disc.addr,resolved,ble_gap_rpa_resolve)) {
            identity.addrType=resolved.type;std::memcpy(identity.addr,resolved.val,6);
        }
    }
    const auto target=BlePeerStore::target();
    const bool wanted=target.keyboard && BlePeerStore::same(identity,target);
    bool hid=false;
    if(parsed) {
        for(unsigned u=0;u<fields.num_uuids16;++u)if(ble_uuid_u16(&fields.uuids16[u].u)==0x1812)hid=true;
        if(fields.appearance_is_present && (fields.appearance>>6)==15)hid=true;
    }
    const bool connectable=disc.event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || disc.event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
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
    // Busy rooms can fill all slots before the controller advertises. Preserve
    // the selected target, then HID devices, then named connectable peers.
    // Replace only a strictly lower priority row; existing peer rows stay put.
    if(index==BleHid::kMaxScanDevices) {
        const auto rank=[](bool target,bool hid,bool named,bool connectable){return target?4:hid?3:connectable?(named?2:1):0;};
        int lowest=rank(wanted,hid,name[0],connectable),replace=-1;
        for(int i=0;i<s_scanCount;++i) {
            const auto &saved=s_scanDevices[i];
            const int score=rank(target.keyboard&&BlePeerStore::same(saved.identity,target),saved.hid,saved.name[0],saved.connectable);
            if(score<lowest){lowest=score;replace=i;}
        }
        if(replace>=0)index=replace;
    }
    if (index < BleHid::kMaxScanDevices) {
        auto &device = s_scanDevices[index];
        if (index == s_scanCount || device.address.addrType!=disc.addr.type || std::memcmp(device.address.addr,disc.addr.val,6)!=0) {
            const bool append=index==s_scanCount;
            device = {};
            device.address.addrType = disc.addr.type;
            std::memcpy(device.address.addr, disc.addr.val, 6);
            if(append) ++s_scanCount;
        }
        device.identity=identity;
        device.rssi = disc.rssi;
        device.seenAtMs=esp_timer_get_time()/1000;
        device.connectable|=connectable;device.hid|=hid;
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
        case BLE_STORE_OBJ_TYPE_CCCD:
        case BLE_STORE_OBJ_TYPE_CSFC:
            // The SDK round-robin callback UNPAIRS an entire other peer even
            // for CCCD/CSFC overflow. Never sacrifice an existing bond here.
            s_bondStoreFull.store(true);
            ESP_LOGW(kTag, "BLE persistent store full (type=%d); bonds preserved",
                     event->overflow.obj_type);
            return BLE_HS_ESTORE_CAP;
        default:
            return BLE_HS_ESTORE_CAP;
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
}
// Safe from GAP callbacks: publish stop intent; only the worker calls adv_stop.
void endConnectionWindow()
{
    s_connectionAllowed.store(false);s_presenceScan.store(false);
    s_advertisingAllowed.store(false);
    s_advPending.store(false); s_advRestart.store(false);
    cancelDiscoverableTimeout();
}

int stopAdvertising()
{
    const int rc=ble_gap_adv_stop();
    if(rc==0) s_advAwaitingResult.store(false);
    return rc;
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

// Host-queue only. Both CONNECT (already encrypted) and ENC_CHANGE use this
// path; the SDK may report restored encryption before application CONNECT.
bool completeSecureConnection(uint16_t handle)
{
    if(handle!=s_connHandle.load() || !s_enabled.load() || s_disconnectPending.load()) return false;
    ble_gap_conn_desc desc{};
    if(ble_gap_conn_find(handle,&desc)!=0 || !desc.sec_state.encrypted) return false;
    auto peer=BlePeerStore::active();
    if(!connectionPeer(desc,peer) || !matchesLinkExpected(peer)) {
        s_privateEligible.store(false);s_disconnectPending.store(true);
        s_pairError.store(BLE_HS_EAUTHEN);s_state.store(BleHidState::Failed);
        (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);return false;
    }
    const bool authenticated=desc.sec_state.encrypted&&desc.sec_state.authenticated&&desc.sec_state.bonded&&desc.sec_state.key_size==16;
    if(s_requireAuthenticated.load()&&!s_keyboardLink.load()&&!authenticated){
        s_privateEligible.store(false);s_disconnectPending.store(true);
        s_pairError.store(BLE_HS_EAUTHEN);s_state.store(BleHidState::Failed);
        (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);return false;
    }
    // Recheck the live key policy even on duplicate/rekey notifications. Only
    // GATT discovery and peer persistence are once-per-link side effects.
    s_privateEligible.store(desc.sec_state.encrypted && desc.sec_state.bonded && desc.sec_state.key_size==16);
    if(s_secureCompletedLink==s_linkGeneration.load()) return true;
    s_secureCompletedLink=s_linkGeneration.load();
    if(s_securityTimerInitialized)ble_npl_callout_stop(&s_securityTimer);
    endConnectionWindow();
    s_pairError.store(0);
    peer.keyboard=s_keyboardLink.load();peer.connected=!peer.keyboard;
    BlePeerStore::activate(peer);BlePeerStore::remember(peer);
    if(peer.keyboard) {
        s_securityDeadlineUs.store(esp_timer_get_time()+20000000);
        BleKeyboardClient::start(handle);
    } else {
        s_securityDeadlineUs.store(0);s_state.store(BleHidState::Connected);
        BlePeerStore::setTarget(peer);s_namePending.store(true);
    }
    return true;
}

void securityFallback(ble_npl_event *)
{
    if(!s_enabled.load() || s_disconnectPending.load() ||
       s_securityLink!=s_linkGeneration.load() || s_securityHandle!=s_connHandle.load() ||
       s_state.load()!=BleHidState::Connecting) return;
    ble_gap_conn_desc desc{};
    if(ble_gap_conn_find(s_securityHandle,&desc)!=0) return;
    if(desc.sec_state.encrypted) { (void)completeSecureConnection(s_securityHandle);return; }
    s_securityRequested=true;
    const int rc=ble_gap_security_initiate(s_securityHandle);
    ESP_LOGI(kTag,"GAP security request rc=%d handle=%u",rc,s_securityHandle);
    // ENOTCONN belongs to the following disconnect event, not a new SMP error.
    if(rc && rc!=BLE_HS_EALREADY && rc!=BLE_HS_ENOTCONN) {
        s_pairError.store(rc);s_state.store(BleHidState::Failed);
        endConnectionWindow();s_disconnectPending.store(true);
        (void)ble_gap_terminate(s_securityHandle,BLE_ERR_REM_USER_CONN_TERM);
    }
}

int gapEvent(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        ESP_LOGI(kTag, "GAP connect status=%d handle=%u", event->connect.status,
                 event->connect.conn_handle);
        const auto windowEnd=s_discoverableDeadlineUs.load();
        bool permitted=s_connectionAllowed.exchange(false) && (!windowEnd || esp_timer_get_time()<windowEnd);
        if(arg && reinterpret_cast<uintptr_t>(arg)!=s_advGeneration.load()) permitted=false;
        s_connectDeadline.store(0);
        if (event->connect.status == 0) {
            ++s_linkGeneration;s_privateEligible.store(false);resetMouseOutput();
            s_verifyExpected=s_targeted.load();s_linkExpected=BlePeerStore::active();
            BleHid::setConnectionHandle(event->connect.conn_handle);
            s_advAwaitingResult.store(false);s_connectPending.store(false);
            struct ble_gap_conn_desc peerDesc{};
            if(ble_gap_conn_find(event->connect.conn_handle,&peerDesc)==0) {
                s_keyboardLink.store(peerDesc.role==BLE_GAP_ROLE_MASTER);
                ESP_LOGI(kTag,"GAP peer role=%s bonded=%u encrypted=%u targeted=%u",
                         s_keyboardLink.load()?"keyboard":"host",peerDesc.sec_state.bonded,
                         peerDesc.sec_state.encrypted,s_targeted.load());
                auto peer=s_linkExpected;
                if(!s_keyboardLink.load()) peer={};
                const bool resolved=connectionPeer(peerDesc,peer,true);peer.connected=false;
                if(!resolved || !matchesLinkExpected(peer)) {
                    if(permitted)s_pairError.store(BLE_HS_EAUTHEN);
                    permitted=false;
                }
                if(permitted)BlePeerStore::activate(peer);
            } else permitted=false;
            if (!s_enabled.load() || !permitted) {
                endConnectionWindow();s_disconnectPending.store(true);
                // A connection that raced with switching Bluetooth off.
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            // A physical link is not yet a usable HID connection. Keep the
            // original manual deadline, but never advertise into this slot.
            // Keyboard connections remain a single active Central attempt.
            if(s_keyboardLink.load()) endConnectionWindow();
            else {
                s_advertisingAllowed.store(false);s_advPending.store(false);s_advRestart.store(false);
            }
            s_lastError.store(0); s_pairError.store(0);
            s_securityDeadlineUs.store(esp_timer_get_time()+(s_keyboardLink.load()?90000000:30000000));
            BleHid::setStateFromCallback(BleHidState::Connecting);
            s_securityRequested=false;
            s_securityHandle=event->connect.conn_handle;s_securityLink=s_linkGeneration.load();
            if(completeSecureConnection(event->connect.conn_handle) || s_disconnectPending.load()) return 0;
            int rc=0;
            if(s_keyboardLink.load()) {
                s_securityRequested=true;
                rc=ble_gap_security_initiate(event->connect.conn_handle);
            } else {
                if(!s_securityTimerInitialized) {
                    rc=ble_npl_callout_init(&s_securityTimer,nimble_port_get_dflt_eventq(),securityFallback,nullptr);
                    if(!rc)s_securityTimerInitialized=true;
                }
                if(!rc)rc=ble_npl_callout_reset(&s_securityTimer,ble_npl_time_ms_to_ticks32(1000));
            }
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(kTag, "Cannot initiate pairing: rc=%d", rc);
                s_pairError.store(rc); s_state.store(BleHidState::Failed);
                endConnectionWindow();s_disconnectPending.store(true);
                (void)ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            s_pairError.store(event->connect.status);s_state.store(s_enabled.load()?BleHidState::Failed:BleHidState::Disabled);
            s_keyboardLink.store(false);BlePeerStore::clearActive();
            endConnectionWindow();s_advAwaitingResult.store(false);s_connectPending.store(false);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGW(kTag, "GAP disconnect reason=%d", event->disconnect.reason);
        if(event->disconnect.conn.conn_handle!=s_connHandle.load()) return 0;
        s_gapTeardown.store(true);
        const bool incomplete=s_state.load()==BleHidState::Connecting;
        const bool established=s_state.load()==BleHidState::Connected;
        const bool cancelled=s_disconnectPending.load() || !s_enabled.load();
        const auto remaining=s_discoverableDeadlineUs.load()-esp_timer_get_time();
        // Resume only the uncompleted, explicitly opened Peripheral invitation,
        // for its remaining time. Never reconnect a working/disconnected link,
        // change a saved target, extend the deadline or retry an SMP failure.
        const bool resume=incomplete && !cancelled && !s_keyboardLink.load() && remaining>0 &&
            !s_pairError.load() && event->disconnect.reason==BLE_HS_HCI_ERR(BLE_ERR_REM_USER_CONN_TERM);
        const auto expected=s_linkExpected;
        ESP_LOGI(kTag,"GAP link ended established=%u local_stop=%u security_requested=%u invitation_remaining_ms=%ld resume=%u",
                 established,cancelled,s_securityRequested,static_cast<long>(std::max<int64_t>(0,remaining)/1000),resume);
        reportMouseSession();
        if(s_securityTimerInitialized)ble_npl_callout_stop(&s_securityTimer);
        s_securityHandle=kInvalidHandle;s_securityRequested=false;
        ++s_linkGeneration;s_privateEligible.store(false);s_namePending.store(false);resetMouseOutput();
        s_linkExpected={};s_verifyExpected=false;
        s_securityDeadlineUs.store(0);
        s_keyboardLink.store(false);s_connectPending.store(false);
        BleKeyboardClient::reset();BlePeerStore::clearActive();
        if(!resume)endConnectionWindow();
        KeyboardInput::reset();
        if(resume) {
            if(s_targeted.load())BlePeerStore::activate(expected);
            s_connectionAllowed.store(true);s_advertisingAllowed.store(true);s_advPending.store(true);
            // Recheck cancellation/deadline after publishing the intent. Poll
            // checks them again before any controller operation.
            if(!s_enabled.load() || s_discoverableDeadlineUs.load()<=esp_timer_get_time())endConnectionWindow();
            s_state.store(!s_enabled.load()?BleHidState::Disabled:
                (s_advertisingAllowed.load()?BleHidState::Connecting:BleHidState::Ready));
        } else {
            if(incomplete && !cancelled && !s_pairError.load())s_pairError.store(event->disconnect.reason);
            BleHid::setStateFromCallback(!s_enabled.load()?BleHidState::Disabled:
                (s_pairError.load()?BleHidState::Failed:BleHidState::Ready));
        }
        clearPairRequest();
        // Publish the free slot last. A worker may immediately accept a new
        // manual request; no old callback state may overwrite that request.
        s_disconnectPending.store(false);
        BleHid::setConnectionHandle(kInvalidHandle);
        s_gapTeardown.store(false);
        return 0;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
        if(event->conn_update.conn_handle!=s_connHandle.load())return 0;
        ble_gap_conn_desc desc{};
        if(ble_gap_conn_find(event->conn_update.conn_handle,&desc)==0) {
            ESP_LOGI(kTag,"GAP interval update status=%d units=%u latency=%u timeout_units=%u",
                     event->conn_update.status,desc.conn_itvl,desc.conn_latency,desc.supervision_timeout);
        }
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(kTag, "GAP encryption changed status=%d handle=%u",
                 event->enc_change.status, event->enc_change.conn_handle);
        if(event->enc_change.conn_handle!=s_connHandle.load() || !s_enabled.load() || s_disconnectPending.load()) return 0;
        // NimBLE emits this while tearing down SMP, BEFORE GAP DISCONNECT.
        // Do not mask the original HCI reason or terminate an already lost link.
        if(event->enc_change.status==BLE_HS_ENOTCONN)return 0;
        clearPairRequest();
        if (event->enc_change.status == BLE_HS_ESTORE_CAP) s_bondStoreFull.store(true);
        if(event->enc_change.status==0) {
            struct ble_gap_conn_desc desc{};
            if(ble_gap_conn_find(event->enc_change.conn_handle,&desc)==0 && desc.sec_state.encrypted) {
                (void)completeSecureConnection(event->enc_change.conn_handle);
            }
        } else {
            s_pairError.store(event->enc_change.status);endConnectionWindow();
            s_state.store(BleHidState::Failed);s_disconnectPending.store(true);
            (void)ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if(!s_keyboardLink.load() || event->notify_rx.conn_handle!=s_connHandle.load()) return 0;
        uint8_t bytes[64];const size_t n=OS_MBUF_PKTLEN(event->notify_rx.om);
        if(n<=sizeof(bytes) && ble_hs_mbuf_to_flat(event->notify_rx.om,bytes,n,nullptr)==0)
            BleKeyboardClient::notification(event->notify_rx.conn_handle,event->notify_rx.attr_handle,bytes,n);
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        const uint8_t action = event->passkey.params.action;
        if (action == BLE_SM_IOACT_NUMCMP) {
            // Secure Connections numeric comparison: show the number and let
            // the user accept or reject it from the Bluetooth screen.
            publishPairRequest(BlePairAction::Confirm,event->passkey.params.numcmp,event->passkey.conn_handle);
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
                ESP_LOGI(kTag, "Pairing code published to local display");
            }
        } else {
            ESP_LOGW(kTag, "Unsupported passkey action=%u", action);
            clearPairRequest();
        }
        return 0;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if(arg && reinterpret_cast<uintptr_t>(arg)!=s_advGeneration.load())return 0;
        if(s_connHandle.load()!=kInvalidHandle || ble_gap_adv_active())return 0;
        endConnectionWindow();
        if(s_connHandle.load()==kInvalidHandle) {
            BlePeerStore::clearActive();
            s_state.store(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
        }
        s_advAwaitingResult.store(false);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Only the live, manually accepted peer may replace its own stale key.
        // This is a peer-requested re-pair, never a boot/flash-time bond reset.
        const auto handle=event->repeat_pairing.conn_handle;
        if(!s_enabled.load() || s_disconnectPending.load() || handle!=s_connHandle.load())
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        struct ble_gap_conn_desc desc{};
        int rc=ble_gap_conn_find(handle,&desc);
        if(rc==0) rc=ble_store_util_delete_peer(&desc.peer_id_addr);
        if(rc!=0) {
            ESP_LOGE(kTag,"Cannot replace peer bond: rc=%d",rc);
            s_pairError.store(rc);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        ESP_LOGI(kTag,"Peer requested re-pairing; replacing only its own bond");
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
    // Keep HID identity in the primary advertisement. The complete local name
    // goes in scan response; no optional TX-power HCI query on every invitation.
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
        if(s_enabled.load() && s_advertisingAllowed.load()) s_advPending.store(true);
    };
    ble_hs_cfg.reset_cb=[](int reason) {
        ESP_LOGW(kTag,"NimBLE host reset reason=%d",reason);
        s_startDeadlineUs.store(esp_timer_get_time()+kHostStartTimeoutUs);
        s_disconnectPending.store(false); s_securityDeadlineUs.store(0);
        if(s_securityTimerInitialized)ble_npl_callout_stop(&s_securityTimer);
        s_securityHandle=kInvalidHandle;s_securityRequested=false;
        s_connHandle.store(kInvalidHandle); clearPairRequest();++s_linkGeneration;s_privateEligible.store(false);resetMouseOutput();
        s_connectPending.store(false);s_keyboardLink.store(false);s_namePending.store(false);
        BleKeyboardClient::reset();BlePeerStore::clearActive();
        s_lastError.store(reason); endConnectionWindow();s_advAwaitingResult.store(false);
        portENTER_CRITICAL(&s_scanLock);
        if(s_scanGeneration!=UINTPTR_MAX) ++s_scanGeneration;
        s_scanState.store(BleScanState::Idle);
        portEXIT_CRITICAL(&s_scanLock);
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
    case ESP_HIDD_CONNECT_EVENT:
        // Worker publishes current battery and subsequent changes; this callback
        // may also run while the shared controller serves an external keyboard.
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        // GAP already cleared the handle and ended the manual window. A delayed
        // HID notification must not clear a newer connection or start it twice.
        break;
    default:
        break;
    }
}

esp_err_t sendReport(uint8_t reportId, uint8_t *data, size_t length)
{
    auto *device = s_device.load();
    if (!BleHid::remoteConnected() || !device || !esp_hidd_dev_connected(device)) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_hidd_dev_input_set(device, 0, reportId, data, length);
}

} // namespace

esp_err_t BleHid::init(void)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if (s_initialized.load()) return ESP_OK;
    // NimBLE emits TWO synchronous INFO lines for every GATT notification.
    // A full USB secondary-console FIFO can busy-wait for 50ms in that path,
    // stalling this same host task before it can serve input or GAP events.
    // Keep warnings/errors and our bounded connection/session diagnostics.
    esp_log_level_set("NimBLE",ESP_LOG_WARN);
    s_state.store(BleHidState::Starting);

    esp_hidd_dev_t *device = nullptr;
    nvs_stats_t nvsStats{};
    esp_err_t err=nvs_get_stats("nvs",&nvsStats);
    // Do not start with an unavailable store and generate unpersisted keys.
    if(err!=ESP_OK) goto fail;
    if(!KeyboardInput::init()) { err=ESP_ERR_NO_MEM;goto fail; }
    if(!s_scanDevices) {
        void *memory=heap_caps_malloc(sizeof(BleScanDevice)*kMaxScanDevices,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!memory) { err=ESP_ERR_NO_MEM;goto fail; }
        s_scanDevices=static_cast<BleScanDevice *>(memory);
        for(int i=0;i<kMaxScanDevices;++i) new(&s_scanDevices[i]) BleScanDevice{};
    }
    err = nimble_port_init();
    if (err != ESP_OK) goto fail;

    BlePeerStore::load(); // SystemService alone schedules the boot-only saved target
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
        int rc=ble_svc_gap_device_name_set(kDeviceName);
        if(rc==0) rc=ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD);
        if(rc!=0) {
            ESP_LOGE(kTag,"Cannot set BLE device identity: rc=%d",rc);
            err=rc==BLE_HS_ENOMEM?ESP_ERR_NO_MEM:ESP_FAIL;
            (void)esp_hidd_dev_deinit(device);
            (void)nimble_port_deinit();
            goto fail;
        }
    }
    ble_store_config_init();
    err = yumo_ble_store_status();
    if (err != ESP_OK) {
        // Never start with an incomplete migration or an unreadable bond store.
        // Retain the persisted records so startup can be retried safely.
        (void)esp_hidd_dev_deinit(device);
        (void)nimble_port_deinit();
        goto fail;
    }
    ble_hs_cfg.store_status_cb = bondStoreStatus;

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
    ESP_LOGE(kTag, "BLE HID init failed: %s (internal=%u largest=%u)", esp_err_to_name(err),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
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
    if(enabled) {
        const bool wasEnabled=s_enabled.exchange(true);
        if(!wasEnabled || s_state.load()==BleHidState::Failed) {
            s_startDeadlineUs.store(esp_timer_get_time()+kHostStartTimeoutUs);
            s_lastError.store(0);s_pairError.store(0);
        }
        const auto err=init();
        if(err!=ESP_OK) { s_enabled.store(false);return err; }
        // Enabling the radio does not select a peer, scan, or permit incoming
        // connections. Preserve a real existing link, including Off/On teardown.
        if(s_connHandle.load()!=kInvalidHandle || s_connectPending.load()) return ESP_OK;
        if(wasEnabled && (s_presenceScan.load() || s_advertisingAllowed.load())) return ESP_OK;
        if(!wasEnabled) { endConnectionWindow();s_presenceScan=false; }
        if(ble_hs_synced()) { s_lastError.store(0);s_state.store(BleHidState::Ready); }
        else s_state.store(BleHidState::Starting);
        return ESP_OK;
    }
    s_enabled.store(false);s_startDeadlineUs.store(0);
    clearPairRequest();endConnectionWindow();stopScan();KeyboardInput::reset();
    if(!s_initialized.load()) { s_state.store(BleHidState::Disabled);return ESP_OK; }
    if(s_connectPending.load()) (void)ble_gap_conn_cancel();
    if(ble_gap_adv_active()) (void)stopAdvertising();
    const uint16_t handle=s_connHandle.load();
    if(handle!=kInvalidHandle && !s_gapTeardown.load()) {
        s_disconnectPending.store(true);
        (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
    }
    // The system BLE stack is retained after first use: callback/GATT teardown
    // is unsafe while in flight. No App instance or radio activity is retained.
    s_state.store(BleHidState::Disabled);
    return ESP_OK;
}

esp_err_t BleHid::disconnect(void)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    endConnectionWindow();stopScan();KeyboardInput::reset();cancelKeys();cancelMouse();cancelPrivateInput();
    s_lastError.store(0);s_pairError.store(0);clearPairRequest();
    if(!s_initialized.load()) return ESP_ERR_INVALID_STATE;
    if(ble_gap_adv_active()) (void)stopAdvertising();
    if(s_connectPending.load()) {
        const int rc=ble_gap_conn_cancel();
        return rc==0?ESP_OK:ESP_FAIL;
    }
    const uint16_t handle=s_connHandle.load();
    if(handle==kInvalidHandle) {
        BlePeerStore::clearActive();
        s_state.store(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
        return ESP_OK;
    }
    if(s_gapTeardown.load() || s_disconnectPending.exchange(true)) return ESP_OK;
    const int rc=ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
    if(rc) s_disconnectPending.store(false);
    return rc==0?ESP_OK:ESP_FAIL;
}

bool BleHid::connectionIdle() {
    return s_connHandle.load()==kInvalidHandle && !s_connectPending.load() &&
        !s_disconnectPending.load() && !s_advAwaitingResult.load() && !s_gapTeardown.load();
}

bool BleHid::keyboardConnected() { return connected() && s_keyboardLink.load() && BleKeyboardClient::keyboardReady(); }
bool BleHid::gamepadConnected() { return connected() && s_keyboardLink.load() && BleKeyboardClient::gamepadReady(); }
bool BleHid::remoteConnected() { return connected() && !s_keyboardLink.load(); }
bool BleHid::waitingForPeer() { return s_enabled.load() && s_targeted.load() && s_advertisingAllowed.load() && s_connHandle.load()==kInvalidHandle; }
BleBond BleHid::peer() { auto peer=BlePeerStore::active();peer.connected=connected();return peer; }
void BleHid::keyboardDiscoveryFinished(bool success,int error) {
    if(!s_keyboardLink.load() || s_connHandle.load()==kInvalidHandle) return;
    if(success && s_enabled.load() && !s_disconnectPending.load()) {
        s_securityDeadlineUs.store(0);s_pairError.store(0);s_state.store(BleHidState::Connected);
        auto peer=BlePeerStore::active();peer.connected=true;BlePeerStore::activate(peer);BlePeerStore::remember(peer);BlePeerStore::setTarget(peer);s_namePending.store(true);
    } else {
        s_pairError.store(error);s_state.store(BleHidState::Failed);endConnectionWindow();
        (void)ble_gap_terminate(s_connHandle.load(),BLE_ERR_REM_USER_CONN_TERM);
    }
}
esp_err_t BleHid::connectKeyboard(const BleScanDevice &device) {
    const std::lock_guard<std::recursive_mutex> lock(s_scanControlMutex);
    if(!s_enabled.load() || !ble_hs_synced() || s_connHandle.load()!=kInvalidHandle || s_connectPending.load()) return ESP_ERR_INVALID_STATE;
    if(!device.connectable || esp_timer_get_time()/1000-device.seenAtMs>15000) return ESP_ERR_NOT_FOUND;
    stopScan();endConnectionWindow();
    s_advertisingAllowed.store(false);s_advPending.store(false);s_advRestart.store(false);cancelDiscoverableTimeout();
    if(ble_gap_adv_active()) {
        const int rc=stopAdvertising();if(rc && rc!=BLE_HS_EALREADY) { s_keyboardLink.store(false);return ESP_FAIL; }
    }
    // A peripheral connection can win just before adv_stop; never reinterpret it.
    if(s_connHandle.load()!=kInvalidHandle) return ESP_ERR_INVALID_STATE;
    s_keyboardLink.store(true);
    auto peer=device.identity;peer.keyboard=true;std::snprintf(peer.name,sizeof(peer.name),"%s",device.name[0]?device.name:device.address.name);
    peer=BlePeerStore::enrich(peer);peer.keyboard=true;BlePeerStore::activate(peer);
    s_requireAuthenticated.store(false);ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;ble_hs_cfg.sm_mitm=0;
    ble_addr_t address{};address.type=device.address.addrType;std::memcpy(address.val,device.address.addr,6);
    uint8_t own=BLE_OWN_ADDR_PUBLIC;
    int rc=ble_hs_id_infer_auto(0,&own);
    if(rc) { s_keyboardLink.store(false);BlePeerStore::clearActive();endConnectionWindow();return ESP_FAIL; }
    s_targeted.store(true);s_connectionAllowed.store(true);s_connectPending.store(true);s_connectDeadline.store(esp_timer_get_time()+12000000);
    s_state.store(BleHidState::Connecting);s_lastError.store(0);s_pairError.store(0);
    rc=ble_gap_connect(own,&address,10000,nullptr,gapEvent,nullptr);
    if(rc) {
        s_connectPending.store(false);s_keyboardLink.store(false);BlePeerStore::clearActive();endConnectionWindow();s_pairError.store(rc);s_state.store(BleHidState::Failed);
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t BleHid::reconnectKeyboard(const BleBond &device) {
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if(!s_enabled.load() || !ble_hs_synced() || s_connHandle.load()!=kInvalidHandle || s_connectPending.load()) return ESP_ERR_INVALID_STATE;
    if(!device.keyboard) return ESP_ERR_INVALID_ARG;
    stopScan();endConnectionWindow();
    if(ble_gap_adv_active() && stopAdvertising()!=0) return ESP_FAIL;
    const auto err=startScan();
    if(err!=ESP_OK) return err;
    s_presenceTarget=device;s_presenceScan=true;
    BlePeerStore::activate(device);
    s_lastError.store(0);s_pairError.store(0);s_state.store(BleHidState::Connecting);
    return ESP_OK;
}

esp_err_t BleHid::reconnectBond(const BleBond &device) {
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if(!s_enabled.load() || !ble_hs_synced() || s_connHandle.load()!=kInvalidHandle || s_connectPending.load()) return ESP_ERR_INVALID_STATE;
    // Revalidate the selected identity, not its old row index/name.
    BleBond paired[kMaxBondSlots]{};const int count=bonds(paired,kMaxBondSlots);
    bool known=false;BleBond selected{};
    for(int i=0;i<count;++i) if(BlePeerStore::same(paired[i],device)) { selected=paired[i];known=true;break; }
    if(!known) return ESP_ERR_NOT_FOUND;
    if(selected.keyboard) return reconnectKeyboard(selected);
    stopScan();endConnectionWindow();
    if(ble_gap_adv_active() && stopAdvertising()!=0) return ESP_FAIL;
    // A host using privacy may not listen to a directed invitation addressed
    // to its saved identity. Advertise normally with the same stable identity;
    // CONNECT still accepts ONLY the selected resolved peer, then restores
    // encryption with its saved keys. The host must initiate the connection.
    s_targeted.store(true);BlePeerStore::activate(selected);
    s_requireAuthenticated.store(false);ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_NO_IO;ble_hs_cfg.sm_mitm=0;
    s_discoverableDeadlineUs.store(esp_timer_get_time()+kDiscoverableDurationUs);
    s_connectionAllowed.store(true);s_advertisingAllowed.store(true);
    s_advRestart.store(true);s_advPending.store(true);
    s_lastError.store(0);s_pairError.store(0);s_state.store(BleHidState::Connecting);
    return ESP_OK;
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
    s_presenceScan=false;
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
    if(count>0) std::memcpy(out, s_scanDevices, count * sizeof(BleScanDevice));
    portEXIT_CRITICAL(&s_scanLock);
    for(int i=0;i<count;++i) {
        out[i].identity=BlePeerStore::enrich(out[i].identity);
        if(!out[i].name[0]) std::snprintf(out[i].name,sizeof(out[i].name),"%s",out[i].identity.name);
    }
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

esp_err_t BleHid::makeDiscoverable(bool authenticated)
{
    const std::lock_guard<std::recursive_mutex> control(s_scanControlMutex);
    if(!s_initialized.load() || !s_enabled.load() || !ble_hs_synced()) return ESP_ERR_INVALID_STATE;
    if(s_connHandle.load()!=kInvalidHandle || s_connectPending.load()) return ESP_ERR_INVALID_STATE;
    stopScan();endConnectionWindow();s_targeted.store(false);BlePeerStore::clearActive();
    s_requireAuthenticated.store(authenticated);
    ble_hs_cfg.sm_io_cap=authenticated?BLE_SM_IO_CAP_DISP_YES_NO:BLE_SM_IO_CAP_NO_IO;ble_hs_cfg.sm_mitm=authenticated?1:0;
    s_discoverableDeadlineUs.store(esp_timer_get_time()+kDiscoverableDurationUs);
    s_discoverable.store(true);s_connectionAllowed.store(true);s_advertisingAllowed.store(true);
    s_advRestart.store(true);s_advPending.store(true);
    s_pairError.store(0);s_lastError.store(0);
    return ESP_OK;
}

esp_err_t BleHid::stopDiscoverable(void)
{
    const bool pending=s_presenceScan.load() || s_connectPending.load();
    endConnectionWindow();
    if(pending) { stopScan();if(s_connectPending.load()) (void)ble_gap_conn_cancel(); }
    const auto handle=s_connHandle.load();
    if(s_gapTeardown.load()) {
        s_state.store(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
        return ESP_OK;
    }
    if(handle!=kInvalidHandle && s_state.load()!=BleHidState::Connected && !s_disconnectPending.exchange(true)) {
        (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
    }
    // Worker poll stops advertising without destroying a connected link.
    if(s_connHandle.load()==kInvalidHandle && !s_connectPending.load()) {
        BlePeerStore::clearActive();s_state.store(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
    }
    return ESP_OK;
}

void BleHid::cancelUserScan()
{
    if(!s_presenceScan) stopScan(); // a manual connect owns its presence probe
}

uint32_t BleHid::discoverableSecondsRemaining(void)
{
    const int64_t deadline = s_discoverableDeadlineUs.load();
    if (!s_discoverable.load() || deadline <= 0) return 0;
    const int64_t remaining = deadline - esp_timer_get_time();
    if (remaining <= 0) return 0;
    return static_cast<uint32_t>((remaining + 999999) / 1000000);
}

// Read-only boot target selection; does not touch HID transport or security keys.
esp_err_t BleHid::bootBond(BleBond &out)
{
    out={};
    if(!s_initialized.load())return ESP_ERR_INVALID_STATE;
    struct Selection {
        BleBond peer{},recent{};
        uint16_t order=0;
        bool found=false,recentFound=false;
    } selected;
    selected.recent=BlePeerStore::target();
    // OUR_SEC is also the source of bonds(). Never compare OUR/PEER counters:
    // NimBLE persists and normalizes each separately. ENC|ID pairing writes
    // these records; normal encrypted reconnection only reads them. A Central
    // bond may keep the usable LTK in PEER_SEC, so do not filter by OUR_SEC LTK.
    const int rc=ble_store_iterate(BLE_STORE_OBJ_TYPE_OUR_SEC,
        [](int,union ble_store_value *value,void *cookie)->int {
            auto &s=*static_cast<Selection *>(cookie);
            const auto &record=value->sec;
            bool nonzero=false;for(auto byte:record.peer_addr.val)nonzero|=byte!=0;
            if(record.peer_addr.type>3 || !nonzero)return 0;
            BleBond candidate{};candidate.addrType=record.peer_addr.type&1;
            std::memcpy(candidate.addr,record.peer_addr.val,sizeof(candidate.addr));
            // Metadata chooses a preference only. A matching live security
            // record is required; names and stale role flags are not identities.
            if(s.recent.addrType<=3 && BlePeerStore::same(candidate,s.recent)) {
                s.recent=candidate;s.recentFound=true;
            }
            if(!s.found || record.bond_count>s.order) {
                s.found=true;s.order=record.bond_count;s.peer=candidate;
            }
            return 0;
        },&selected);
    if(rc) {
        ESP_LOGW(kTag,"Cannot read boot bonds: rc=%d",rc);
        return ESP_FAIL; // never use a partial snapshot after a store error
    }
    if(!selected.found)return ESP_ERR_NOT_FOUND;
    out=BlePeerStore::enrich(selected.recentFound?selected.recent:selected.peer);
    ESP_LOGI(kTag,"Boot target selected from %s",selected.recentFound?"last successful connection":"pairing order");
    return ESP_OK;
}

uintptr_t BleHid::privateLinkGeneration(){return remoteConnected()&&s_privateEligible.load()?s_linkGeneration.load():0;}
uint32_t BleHid::beginPrivateInput(){return ++s_privatePermit;}
void BleHid::cancelPrivateInput(){++s_privatePermit;}
bool BleHid::privateInputIdle(){return !s_privateStroke.busy.load();}
esp_err_t BleHid::sendPrivateKey(uint8_t key,uint8_t modifiers,uintptr_t link,uint32_t permit){
    if(!key||!link||link!=privateLinkGeneration()||permit!=s_privatePermit.load()||!s_hostRunning.load())return ESP_ERR_INVALID_STATE;
    // Release the producer lock before waiting; callbacks must stay independent.
    std::unique_lock<std::mutex> channel(s_keySubmitMutex);
    if(s_keys.running.load() || s_keys.blockedLink.load()==link)return ESP_ERR_INVALID_STATE;
    auto &s=s_privateStroke;if(s.busy.exchange(true))return ESP_ERR_INVALID_STATE;
    if(!s.initialized){if(ble_npl_callout_init(&s.timer,nimble_port_get_dflt_eventq(),privateStrokeEvent,nullptr)!=0){s.busy.store(false);return ESP_ERR_NO_MEM;}s.initialized=true;}
    s.key=key;s.modifiers=modifiers;s.link=link;s.permit=permit;s.releasing=false;s.result.store(ESP_OK);s.done.store(false);
    if(ble_npl_callout_reset(&s.timer,1)!=0){s.key=0;s.modifiers=0;s.busy.store(false);s.done.store(true);return ESP_FAIL;}
    channel.unlock();
    const int64_t deadline=esp_timer_get_time()+2000000;
    while(!s.done.load()&&esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(10));
    if(!s.done.load()){cancelPrivateInput();return ESP_ERR_TIMEOUT;}
    return s.result.load();
}

esp_err_t BleHid::sendKey(uint8_t keycode, uint8_t modifiers)
{
    const std::lock_guard<std::mutex> channel(s_keySubmitMutex);
    if(!keycode || keycode>0x65)return ESP_ERR_INVALID_ARG;
    if(!remoteConnected() || !s_hostRunning.load() || !Power::isScreenOn() ||
       Power::sleepPending() || s_privateStroke.busy.load())return ESP_ERR_INVALID_STATE;
    auto &s=s_keys;
    if(!s.initialized) {
        if(ble_npl_callout_init(&s.timer,nimble_port_get_dflt_eventq(),keyStrokeEvent,nullptr)!=0)return ESP_ERR_NO_MEM;
        s.initialized=true;
    }
    const hid::QueuedKey key{keycode,modifiers,s_linkGeneration.load(),s.epoch.load()};
    if(s.blockedLink.load()==key.link)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_keyQueueLock);
    if(!s.queue.push(key)) { portEXIT_CRITICAL(&s_keyQueueLock);return ESP_ERR_NO_MEM; }
    const bool start=!s.running.exchange(true);
    portEXIT_CRITICAL(&s_keyQueueLock);
    if(start && ble_npl_callout_reset(&s.timer,1)!=0) {
        // No callback/active report exists on this initial scheduling failure.
        portENTER_CRITICAL(&s_keyQueueLock);
        s.queue.clear();s.running.store(false);
        portEXIT_CRITICAL(&s_keyQueueLock);
        return ESP_FAIL;
    }
    return ESP_OK; // accepted; asynchronous errors are consumed by the App rail
}
void BleHid::cancelKeys() { ++s_keys.epoch; }
bool BleHid::keysIdle() { return !s_keys.running.load(); }
esp_err_t BleHid::takeKeyError() { return s_keys.error.exchange(ESP_OK); }

uintptr_t BleHid::mouseLinkGeneration() {
    const auto link=s_linkGeneration.load();
    return remoteConnected()&&link==s_linkGeneration.load()?link:0;
}
esp_err_t BleHid::sendMouse(int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons,uintptr_t expectedLink)
{
    const std::lock_guard<std::mutex> producer(s_mouseSubmitMutex);
    if(buttons&~7u)return ESP_ERR_INVALID_ARG;
    if(!remoteConnected() || !s_hostRunning.load() || Power::sleepPending())return ESP_ERR_INVALID_STATE;
    auto &s=s_mouse;
    if(!s.initialized) {
        if(ble_npl_callout_init(&s.timer,nimble_port_get_dflt_eventq(),mouseOutputEvent,nullptr)!=0)return ESP_ERR_NO_MEM;
        s.initialized=true;
    }
    const auto link=s_linkGeneration.load();
    portENTER_CRITICAL(&s_mouseLock);
    if((expectedLink && expectedLink!=link) || link!=s_linkGeneration.load()){
        portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_INVALID_STATE;
    }
    if(s.blockedLink==link){portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_INVALID_STATE;}
    if(s.link!=link){s.queue.reset();s.link=link;s.scheduled=false;}
    if(!s.queue.push(dx,dy,wheel,buttons)){portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_NO_MEM;}
    const bool start=s.queue.pending()&&!s.scheduled;
    if(start)s.scheduled=true;
    s.running.store(!s.queue.idle());
    portEXIT_CRITICAL(&s_mouseLock);
    if(start && ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(mouseScheduleDelayMs()))!=0) {
        // No previously scheduled report exists here. A prior held button
        // must be released by the host, never by this GUI/producer task.
        portENTER_CRITICAL(&s_mouseLock);
        s.queue.cancel();s.scheduled=false;s.running.store(!s.queue.idle());
        portEXIT_CRITICAL(&s_mouseLock);
        s.error.store(ESP_FAIL);return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t BleHid::clickMouse(uint8_t button,uintptr_t expectedLink)
{
    const std::lock_guard<std::mutex> producer(s_mouseSubmitMutex);
    if(!button || (button&~7u))return ESP_ERR_INVALID_ARG;
    if(!remoteConnected() || !s_hostRunning.load() || Power::sleepPending())return ESP_ERR_INVALID_STATE;
    auto &s=s_mouse;
    if(!s.initialized) {
        if(ble_npl_callout_init(&s.timer,nimble_port_get_dflt_eventq(),mouseOutputEvent,nullptr)!=0)return ESP_ERR_NO_MEM;
        s.initialized=true;
    }
    const auto link=s_linkGeneration.load();
    portENTER_CRITICAL(&s_mouseLock);
    if((expectedLink && expectedLink!=link) || link!=s_linkGeneration.load()){
        portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_INVALID_STATE;
    }
    if(s.blockedLink==link){portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_INVALID_STATE;}
    if(s.link!=link){s.queue.reset();s.link=link;s.scheduled=false;}
    if(!s.queue.click(button)){portEXIT_CRITICAL(&s_mouseLock);return ESP_ERR_NO_MEM;}
    const bool start=!s.scheduled;s.scheduled=true;s.running.store(true);
    portEXIT_CRITICAL(&s_mouseLock);
    if(start && ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(mouseScheduleDelayMs()))!=0) {
        portENTER_CRITICAL(&s_mouseLock);
        s.queue.cancel();s.scheduled=false;s.running.store(!s.queue.idle());
        portEXIT_CRITICAL(&s_mouseLock);
        s.error.store(ESP_FAIL);return ESP_FAIL;
    }
    return ESP_OK;
}
void BleHid::cancelMouse() {
    const std::lock_guard<std::mutex> producer(s_mouseSubmitMutex);
    auto &s=s_mouse;
    portENTER_CRITICAL(&s_mouseLock);
    s.queue.cancel();
    if(s.link!=s_linkGeneration.load() || !remoteConnected())s.queue.reset();
    const bool start=s.initialized&&s.queue.pending()&&!s.scheduled;
    if(start)s.scheduled=true;
    s.running.store(!s.queue.idle());
    portEXIT_CRITICAL(&s_mouseLock);
    if(start && ble_npl_callout_reset(&s.timer,ble_npl_time_ms_to_ticks32(mouseScheduleDelayMs()))!=0) {
        portENTER_CRITICAL(&s_mouseLock);s.scheduled=false;portEXIT_CRITICAL(&s_mouseLock);
        s.error.store(ESP_FAIL);
    }
}
bool BleHid::mouseIdle(){return !s_mouse.running.load();}
esp_err_t BleHid::takeMouseError(){return s_mouse.error.exchange(ESP_OK);}

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
    BleBond active = {};
    const uint16_t handle = s_connHandle.load();
    if (handle != kInvalidHandle) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(handle, &desc) == 0) {
            haveActive = connectionPeer(desc,active);
        }
    }

    for (int i = 0; i < found; ++i) {
        out[i]={};out[i].addrType = addrs[i].type;
        std::memcpy(out[i].addr, addrs[i].val, sizeof(out[i].addr));
        out[i].connected = connected() && haveActive && BlePeerStore::same(active,out[i]);
    }
    for(int i=0;i<found;++i) out[i]=BlePeerStore::enrich(out[i]);
    return found;
}

esp_err_t BleHid::removeBond(const BleBond &bond)
{
    if (!s_initialized.load()) return ESP_ERR_INVALID_STATE;
    esp_err_t storeError = yumo_ble_store_status();
    if (storeError != ESP_OK) { s_lastError.store(storeError); return storeError; }

    ble_addr_t addr = {};
    addr.type = bond.addrType;
    std::memcpy(addr.val, bond.addr, sizeof(addr.val));

    // ble_gap_unpair() terminates the connection, drops the keys and removes the
    // identity from the controller resolving list. Fall back to a plain store
    // delete if it reports the peer is not known to GAP.
    int rc = ble_gap_unpair(&addr);
    if (rc != 0 && yumo_ble_store_status() == ESP_OK) {
        ESP_LOGW(kTag, "ble_gap_unpair failed rc=%d; deleting store records", rc);
        rc = ble_store_util_delete_peer(&addr);
    }
    storeError = yumo_ble_store_status();
    if (storeError != ESP_OK) {
        s_lastError.store(storeError);
        ESP_LOGE(kTag, "Cannot persist bond removal: %s", esp_err_to_name(storeError));
        return storeError;
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot remove bond: rc=%d", rc);
        return ESP_FAIL;
    }
    // The pinned SDK unpair path can swallow a store deletion error. Confirm
    // both security directions are gone before forgetting the visible device.
    ble_store_key_sec key{};
    key.peer_addr = addr;
    ble_store_value_sec value{};
    const int ourRc = ble_store_read_our_sec(&key, &value);
    const int peerRc = ble_store_read_peer_sec(&key, &value);
    if (ourRc != BLE_HS_ENOENT || peerRc != BLE_HS_ENOENT) {
        ESP_LOGE(kTag, "Bond removal incomplete: our_rc=%d peer_rc=%d", ourRc, peerRc);
        return ESP_FAIL;
    }
    const auto target=BlePeerStore::target();
    if(BlePeerStore::same(target,bond) || BlePeerStore::same(BlePeerStore::active(),bond)) {
        endConnectionWindow();stopScan();
        if(s_connectPending.load()) (void)ble_gap_conn_cancel();
        if(s_connHandle.load()==kInvalidHandle && !s_connectPending.load())
            s_state.store(s_enabled.load()?BleHidState::Ready:BleHidState::Disabled);
    }
    BlePeerStore::forget(bond);
    s_bondStoreFull.store(false);
    return ESP_OK;
}

esp_err_t BleHid::removeAllBonds(void)
{
    if (!s_initialized.load()) return ESP_ERR_INVALID_STATE;
    const esp_err_t storeError = yumo_ble_store_status();
    if (storeError != ESP_OK) { s_lastError.store(storeError); return storeError; }

    esp_err_t firstErr = ESP_OK;
    ble_addr_t list[kMaxBondSlots] = {};
    int count = 0;
    if (ble_store_util_bonded_peers(list, &count, kMaxBondSlots) != 0) return ESP_FAIL;
    for (int i = 0; i < count; ++i) {
        BleBond bond{};
        bond.addrType = list[i].type;
        std::memcpy(bond.addr, list[i].val, sizeof(bond.addr));
        const esp_err_t err = removeBond(bond);
        if (err != ESP_OK && firstErr == ESP_OK) firstErr = err;
    }
    if (firstErr == ESP_OK) s_bondStoreFull.store(false);
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
    if(s_advertisingAllowed.load()) s_advPending.store(true); // callbacks never mutate advertisement fields
}
int BleHid::lastError() { const int radio=s_lastError.load(); return radio?radio:s_pairError.load(); }
void BleHid::poll()
{
    recoverMouseSchedule();
    if(!s_initialized.load()) return;
    const auto now=esp_timer_get_time();
    if(now>=s_nextPeerFlush) { s_nextPeerFlush=now+5000000;BlePeerStore::flush(); }
    if(!s_enabled.load()) return;
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
    if(s_state.load()==BleHidState::Starting) s_state.store(BleHidState::Ready);
    const auto windowEnd=s_discoverableDeadlineUs.load();
    if(windowEnd && now>=windowEnd) {
        endConnectionWindow();
        if(s_connHandle.load()==kInvalidHandle && !s_connectPending.load()) {
            BlePeerStore::clearActive();s_state.store(BleHidState::Ready);
        }
    }
    if(!s_advertisingAllowed.load() && ble_gap_adv_active()) {
        (void)stopAdvertising();
        if(s_connHandle.load()==kInvalidHandle && !s_connectPending.load() && !s_presenceScan) {
            BlePeerStore::clearActive();
            if(s_state.load()!=BleHidState::Failed) s_state.store(BleHidState::Ready);
        }
    }
    const auto handle=s_connHandle.load();
    if(handle!=kInvalidHandle) {
        publishBattery(now);
        if(connected() && s_namePending.exchange(false)) {
            const ble_uuid16_t nameUuid=BLE_UUID16_INIT(0x2a00);
            (void)ble_gattc_read_by_uuid(handle,1,0xffff,&nameUuid.u,nameRead,reinterpret_cast<void *>(s_linkGeneration.load()));
        }
        const auto deadline=s_securityDeadlineUs.load();
        if(s_state.load()!=BleHidState::Connected && deadline && now>=deadline) {
            s_securityDeadlineUs.store(now+1000000);
            s_pairError.store(BLE_HS_ETIMEOUT); s_state.store(BleHidState::Failed);
            endConnectionWindow();s_disconnectPending.store(true);
            (void)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
        }
        return;
    }
    if(s_connectPending.load()) {
        if(now>=s_connectDeadline.load()) { endConnectionWindow();(void)ble_gap_conn_cancel();s_connectDeadline.store(now+1000000); }
        return;
    }
    if(s_presenceScan) {
        BleScanDevice results[kMaxScanDevices]{};const int count=scanResults(results,kMaxScanDevices);
        for(int i=0;i<count;++i) if(results[i].connectable && BlePeerStore::same(results[i].identity,s_presenceTarget)) {
            const auto err=connectKeyboard(results[i]);
            if(err!=ESP_OK) { s_presenceScan=false;s_lastError.store(err);s_state.store(BleHidState::Failed);BlePeerStore::clearActive(); }
            return;
        }
        if(scanState()!=BleScanState::Scanning) {
            s_presenceScan=false;BlePeerStore::clearActive();
            s_lastError.store(ESP_ERR_NOT_FOUND);s_state.store(BleHidState::Failed);
        }
        return; // No retry timer: another attempt requires another tap.
    }
    if(now<s_nextAdvAttempt) return;
    // Permission is not a restart request. The controller stops advertising
    // before the CONNECT callback publishes our handle. Polling that gap must
    // not start a second connectable advertisement into the occupied slot.
    // Drain redundant requests while already active (including late HID_START).
    const bool restart=s_advRestart.exchange(false);
    const bool requested=s_advPending.exchange(false);
    if(!restart && !requested) return;
    if(restart && ble_gap_adv_active()) (void)stopAdvertising();
    if(!ble_gap_adv_active() && s_advertisingAllowed.load()) {
        s_nextAdvAttempt=now+1000000;
        processAdvertising();
    }
}
namespace {
void processAdvertising()
{
    if (!s_enabled.load() || !s_advertisingAllowed.load() || !s_connectionAllowed.load() ||
        s_connHandle.load()!=kInvalidHandle || !ble_hs_synced() || ble_gap_adv_active()) return;

    uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    int rc = ble_hs_id_infer_auto(0, &ownAddrType);
    if (rc != 0) {
        ESP_LOGE(kTag, "Cannot infer BLE address type: rc=%d", rc);
        s_lastError.store(rc); s_state.store(BleHidState::Failed);
        endConnectionWindow(); // fail closed; retry requires a new manual request
        return;
    }
    s_ownAddrType.store(ownAddrType);

    const bool targeted=s_targeted.load();
    // Both explicit invitations must be discoverable to the phone. Targeted
    // changes the identity acceptance rule, not the GAP discovery flags.
    const bool discoverable=s_discoverable.load() || targeted;
    // A saved phone is a Central, not a device to find with our keyboard scan.
    // Filter connection requests in the controller before an unrelated saved
    // computer can occupy our only link and consume the phone's invitation.
    // Public discovery still accepts new peers. IRK resolution stays in NimBLE;
    // the CONNECT/encryption callbacks retain their independent identity check.
    if(targeted) {
        const auto target=BlePeerStore::active();
        ble_addr_t address{},identity{};
        address.type=target.addrType;std::memcpy(address.val,target.addr,6);
        if(target.keyboard || !ble::resolveIdentity(address,identity,ble_gap_rpa_resolve)) rc=BLE_HS_EINVAL;
        else rc=ble_gap_wl_set(&identity,1);
        if(rc) {
            ESP_LOGW(kTag,"Cannot select BLE peer for invitation: rc=%d",rc);
            s_lastError.store(rc);s_state.store(BleHidState::Failed);endConnectionWindow();return;
        }
    }
    auto fields=s_advFields;
    fields.flags=BLE_HS_ADV_F_BREDR_UNSUP | (discoverable?BLE_HS_ADV_F_DISC_GEN:0);
    rc=ble_gap_adv_set_fields(&fields);
    if(!rc) {
        ble_hs_adv_fields response{};
        response.name=reinterpret_cast<uint8_t *>(const_cast<char *>(kDeviceName));
        response.name_len=sizeof(kDeviceName)-1;response.name_is_complete=1;
        rc=ble_gap_adv_rsp_set_fields(&response);
    }
    if(rc) {
        ESP_LOGW(kTag,"Cannot prepare BLE discovery data: rc=%d",rc);
        s_lastError.store(rc);s_state.store(BleHidState::Failed);endConnectionWindow();return;
    }
    struct ble_gap_adv_params params={};
    params.conn_mode=BLE_GAP_CONN_MODE_UND;
    params.disc_mode=discoverable?BLE_GAP_DISC_MODE_GEN:BLE_GAP_DISC_MODE_NON;
    params.high_duty_cycle=0;
    // Scan requests remain open so iOS can read the name. Only connection
    // requests are filtered, and only for an explicitly selected saved peer.
    params.filter_policy=targeted?BLE_HCI_ADV_FILT_CONN:BLE_HCI_ADV_FILT_NONE;
    params.itvl_min=ble::ManualAdvertisingInterval;
    params.itvl_max=ble::ManualAdvertisingInterval;
    const auto remaining=(s_discoverableDeadlineUs.load()-esp_timer_get_time())/1000;
    if(remaining<=0) { endConnectionWindow();s_state.store(BleHidState::Ready);return; }
    auto previousState=s_state.load();
    const auto generation=++s_advGeneration;
    s_advAwaitingResult.store(true);
    rc = ble_gap_adv_start(s_ownAddrType.load(), nullptr, static_cast<int32_t>(remaining),
                           &params, gapEvent, reinterpret_cast<void *>(generation));
    if(rc && rc!=BLE_HS_EALREADY) s_advAwaitingResult.store(false);
    // A real connection may win while the controller handles adv_start.
    // Never replace the GAP callback's Connected state with a spurious failure.
    if(s_connHandle.load()!=kInvalidHandle) return;
    const bool advertising=rc==0 || rc==BLE_HS_EALREADY;
    if (!advertising) {
        endConnectionWindow(); // failure never creates a background retry
        ESP_LOGE(kTag,"Cannot start advertising: rc=%d",rc);
    }
    else if (rc==0) ESP_LOGI(kTag, "Advertising as %s", kDeviceName);
    s_lastError.store(advertising?0:rc);
    // CONNECT/ENC_CHANGE callbacks own newer state even between the handle
    // check above and this publication. Never overwrite it from task context.
    s_state.compare_exchange_strong(previousState,advertising ? (targeted?BleHidState::Connecting:BleHidState::Advertising) : BleHidState::Failed);
}

} // namespace
esp_err_t BleHid::prepareDeepSleep()
{
    (void)setEnabled(false);
    BlePeerStore::flush();
    if(!s_initialized.load()) return ESP_OK;
    // Terminal path only; no deinit/free of GATT while callbacks may run.
    auto err=nimble_port_stop();
    if(err!=ESP_OK) return err;
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED)
        return esp_bt_controller_disable();
    return ESP_OK;
}
} // namespace sys
