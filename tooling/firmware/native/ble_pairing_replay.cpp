// Real application GAP callback + installed SDK disconnect/SMP teardown order.
// Radio, stores, GATT and RTOS timer are deterministic test doubles.
#include <cstdint>
#include <cstring>
#include <atomic>
constexpr int BLE_HS_ENOTCONN=7,BLE_HS_EAUTHEN=23,BLE_HS_ESTORE_CAP=27;
constexpr int BLE_HS_ETIMEOUT=13;
constexpr int BLE_HS_EAGAIN=1;
constexpr int BLE_GAP_EVENT_CONNECT=1,BLE_GAP_EVENT_DISCONNECT=2,BLE_GAP_EVENT_ENC_CHANGE=3;
constexpr int BLE_GAP_EVENT_NOTIFY_RX=4,BLE_GAP_EVENT_PASSKEY_ACTION=5;
constexpr int BLE_GAP_EVENT_ADV_COMPLETE=6,BLE_GAP_EVENT_REPEAT_PAIRING=7;
constexpr int BLE_GAP_EVENT_CONN_UPDATE=8;
constexpr int BLE_SM_IOACT_NUMCMP=4,BLE_SM_IOACT_DISP=3;
constexpr int BLE_GAP_REPEAT_PAIRING_IGNORE=0,BLE_GAP_REPEAT_PAIRING_RETRY=1;
constexpr int BLE_GAP_ROLE_MASTER=0;
#define BLE_HS_HCI_ERR(x) (0x200+(x))
struct ble_gap_conn_desc {
    uint16_t conn_handle=1,conn_itvl=12,conn_latency=0,supervision_timeout=600;uint8_t role=1;
    struct {uint8_t encrypted=0,authenticated=0,bonded=0,key_size=0;}sec_state;
    struct {uint8_t type=0,val[6]{};}peer_id_addr;
};
struct ble_gap_event {
    int type=0;
    struct {int status=0;uint16_t conn_handle=1;}connect,enc_change,conn_update;
    struct {ble_gap_conn_desc conn{};int reason=0;}disconnect;
    struct {uint16_t conn_handle=1,attr_handle=0;void *om=nullptr;}notify_rx;
    struct {uint16_t conn_handle=1;struct {uint8_t action=0;uint32_t numcmp=0;}params;}passkey;
    struct {uint16_t conn_handle=1;}repeat_pairing;
};
struct ble_npl_event{};
struct ble_npl_callout {bool armed=false;unsigned delay=0;void(*cb)(ble_npl_event*)=nullptr;};
struct ble_sm_io {uint8_t action=0;uint32_t passkey=0;bool numcmp_accept=false;};
#include "pairing_radio.inc"
namespace fake {
ble_gap_conn_desc desc{};bool present=true,identityValid=true,expectedValid=true;
unsigned securityRequests=0,remembered=0,keyboardStarts=0,pairClears=0,targetWrites=0;
sys::BleBond lastTarget{};
int securityError=0;uint8_t report[64]{};
void(*onHandleReleased)()=nullptr;
void(*onPairCleared)()=nullptr;
}
int ble_gap_conn_find(uint16_t h,ble_gap_conn_desc *out){if(!fake::present||h!=fake::desc.conn_handle)return BLE_HS_ENOTCONN;*out=fake::desc;return 0;}
int ble_gap_security_initiate(uint16_t){++fake::securityRequests;return fake::securityError;}
void *nimble_port_get_dflt_eventq(){return nullptr;}
int ble_npl_callout_init(ble_npl_callout *t,void*,void(*cb)(ble_npl_event*),void*){t->cb=cb;return 0;}
int ble_npl_callout_reset(ble_npl_callout *t,unsigned delay){t->armed=true;t->delay=delay;return 0;}
void ble_npl_callout_stop(ble_npl_callout *t){t->armed=false;}
unsigned ble_npl_time_ms_to_ticks32(unsigned n){return n;}
unsigned esp_random(){return 123456;}
int ble_sm_inject_io(uint16_t,ble_sm_io*){return 0;}
int ble_store_util_delete_peer(const void*){return 0;}
#define OS_MBUF_PKTLEN(x) 0
int ble_hs_mbuf_to_flat(void*,void*,size_t,void*){return 0;}
namespace sys {
std::atomic<uintptr_t> s_linkGeneration{0};
std::atomic<bool> s_keyboardLink{false},s_privateEligible{false},s_namePending{false},s_bondStoreFull{false};
std::atomic<int64_t> s_connectDeadline{0},s_securityDeadlineUs{0};
BleBond s_linkExpected{};bool s_verifyExpected=false;
ble_npl_callout s_securityTimer{};bool s_securityTimerInitialized=false;
uintptr_t s_securityLink=0;uint16_t s_securityHandle=kInvalidHandle;bool s_securityRequested=false;
uintptr_t s_secureCompletedLink=0;
// Test doubles for the unrelated external-keyboard and UI implementations.
struct BleKeyboardClient {static void reset(){}static void start(uint16_t){++fake::keyboardStarts;}static void notification(uint16_t,uint16_t,uint8_t*,size_t){}};
struct KeyboardInput {static void reset(){}};
void clearPairRequest(){++fake::pairClears;if(fake::onPairCleared)fake::onPairCleared();}
void publishPairRequest(BlePairAction,uint32_t,uint16_t){}
bool connectionPeer(const ble_gap_conn_desc&,BleBond &peer,bool=false){peer=phone();return fake::identityValid;}
bool matchesLinkExpected(const BleBond&){return fake::expectedValid;}
void BlePeerStore::remember(const BleBond&){++fake::remembered;}
void BlePeerStore::setTarget(const BleBond& b){fake::lastTarget=b;++fake::targetWrites;}
void BleHid::setConnectionHandle(uint16_t h){s_connHandle=h;if(h==kInvalidHandle&&fake::onHandleReleased)fake::onHandleReleased();}
void BleHid::setStateFromCallback(BleHidState s){s_state=s;}
void resetMouseOutput(){}
void reportMouseSession(){}
#include "pairing.inc"
void BleHid::cancelKeys(){}
void BleHid::cancelMouse(){}
void BleHid::cancelPrivateInput(){}
#include "connection_lifecycle.inc"
}

// Minimal SDK module context; the real functions below fix the ordering.
#define NIMBLE_BLE_CONNECT 1
#define MYNEWT_VAL(x) 0
#define STATS_INC(...) ((void)0)
constexpr unsigned BLE_HS_CONN_F_MASTER=1;
struct ble_gap_update_entry{};
struct ble_gap_snapshot{ble_gap_conn_desc *desc;int(*cb)(ble_gap_event*,void*);void *cb_arg;};
struct ble_hs_conn {unsigned bhc_flags=0,slave_conn=1;int bhc_max_tx_time=0,bhc_max_rx_time=0,bhc_max_tx_octets=0,bhc_max_rx_octets=0;}sdkConn;
struct ble_sm_result {int app_status=0,enc_cb=0;};
void ble_hs_lock(){}void ble_hs_unlock(){}
ble_gap_update_entry *ble_gap_update_entry_remove(uint16_t){return nullptr;}
void ble_gap_update_notify(uint16_t,int){}void ble_gap_update_entry_free(ble_gap_update_entry*){}
int ble_gap_find_snapshot(uint16_t h,ble_gap_snapshot *s){if(ble_gap_conn_find(h,s->desc))return 7;s->cb=sys::gapEvent;s->cb_arg=nullptr;return 0;}
void ble_l2cap_sig_conn_broken(uint16_t,int){}void ble_hs_flow_connection_broken(uint16_t){}
ble_hs_conn *ble_hs_conn_find(uint16_t){return fake::present?&sdkConn:nullptr;}
void ble_gap_event_connect_call(uint16_t,int){std::abort();}
int ble_hs_atomic_conn_delete(uint16_t){fake::present=false;return 0;}
void ble_gap_event_listener_call(ble_gap_event*){}
void ble_gap_call_event_cb(ble_gap_event *e,int(*cb)(ble_gap_event*,void*),void *arg){cb(e,arg);}
// Only the SMP procedure internals are substituted, forwarding exactly the
// enc_cb/app_status that the actual SDK connection_broken creates.
void ble_sm_process_result(uint16_t h,ble_sm_result *res,bool){
    CHECK(res->enc_cb&&res->app_status==BLE_HS_ENOTCONN);
    ble_gap_event e{};e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=h;e.enc_change.status=res->app_status;
    sys::gapEvent(&e,nullptr);
}
#include "sdk_sm_broken.inc"
#include "sdk_gap_broken.inc"

void resetPair(){
    reset();fake::desc={};fake::present=true;fake::identityValid=fake::expectedValid=true;
    fake::securityRequests=fake::remembered=fake::keyboardStarts=fake::pairClears=fake::targetWrites=0;fake::lastTarget={};fake::securityError=0;sdkConn={};
    fake::onHandleReleased=nullptr;fake::onPairCleared=nullptr;
    using namespace sys;s_keyboardLink=false;s_privateEligible=false;s_namePending=false;
    s_securityDeadlineUs=0;s_securityTimer.armed=false;s_securityRequested=false;s_verifyExpected=false;
    s_advAwaitingResult=false;s_secureCompletedLink=0;
}
void openAndConnect(bool targeted=false,bool keyboard=false){
    using namespace sys;
    CHECK(targeted?BleHid::reconnectBond(phone())==0:BleHid::makeDiscoverable()==0);
    processAdvertising();fake::advertising=false;
    if(keyboard)fake::desc.role=BLE_GAP_ROLE_MASTER;
    ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;gapEvent(&e,fake::callbackArg);
}
void encrypted(){
    fake::desc.sec_state.encrypted=1;fake::desc.sec_state.bonded=1;fake::desc.sec_state.key_size=16;
    ble_gap_event e{};e.type=BLE_GAP_EVENT_ENC_CHANGE;sys::gapEvent(&e,nullptr);
}
int main(){
    using namespace sys;
    // Actual observed failure: encryption is restored BEFORE application CONNECT.
    // Each cycle alternates a saved host and open discovery, including reused handles.
    for(unsigned i=0;i<200;++i) {
        resetPair();CHECK(i%2?BleHid::reconnectBond(phone())==0:BleHid::makeDiscoverable()==0);
        processAdvertising();fake::advertising=false;
        encrypted();CHECK(s_connHandle==kInvalidHandle&&fake::remembered==0);
        ble_gap_event connect{};connect.type=BLE_GAP_EVENT_CONNECT;gapEvent(&connect,nullptr);
        CHECK(s_state==BleHidState::Connected&&s_privateEligible&&!s_securityDeadlineUs);
        CHECK(!s_securityTimer.armed&&fake::securityRequests==0&&fake::remembered==1);
        CHECK(fake::targetWrites==1&&BlePeerStore::same(fake::lastTarget,phone()));
        encrypted();encrypted();CHECK(fake::remembered==1&&!fake::keyboardStarts);
        fake::now+=31000000;CHECK(s_state==BleHidState::Connected&&!s_securityDeadlineUs);
        ble_gap_conn_broken(1,531);CHECK(!s_advertisingAllowed&&s_state==BleHidState::Ready);
    }
    // An early encrypted result cannot bypass identity, authorization or key policy.
    resetPair();encrypted();fake::expectedValid=false;openAndConnect(true);
    CHECK(!s_privateEligible&&s_state!=BleHidState::Connected&&fake::terminations==1);
    resetPair();openAndConnect();fake::desc.sec_state.encrypted=1;fake::desc.sec_state.bonded=1;
    fake::desc.sec_state.key_size=16;s_securityTimer.cb(nullptr);
    CHECK(s_state==BleHidState::Connected&&fake::securityRequests==0&&fake::remembered==1);
    resetPair();openAndConnect(false,true);encrypted();encrypted();CHECK(fake::keyboardStarts==1);
    resetPair();openAndConnect();encrypted();fake::desc.sec_state.key_size=8;
    ble_gap_event rekey{};rekey.type=BLE_GAP_EVENT_ENC_CHANGE;gapEvent(&rekey,nullptr);
    CHECK(!s_privateEligible&&fake::remembered==1);
    fake::expectedValid=false;gapEvent(&rekey,nullptr);
    CHECK(s_disconnectPending&&s_state==BleHidState::Failed);
    // Controller already accepted a link but app CONNECT is delayed. A manual
    // switch must wait through CONNECT + DISCONNECT, not reuse the empty handle.
    resetPair();CHECK(!BleHid::makeDiscoverable());processAdvertising();
    CHECK(!BleHid::connectionIdle());fake::advertising=false;
    CHECK(!BleHid::disconnect());CHECK(!BleHid::connectionIdle());
    ble_gap_event late{};late.type=BLE_GAP_EVENT_CONNECT;gapEvent(&late,fake::callbackArg);
    CHECK(fake::terminations==1&&!BleHid::connectionIdle()&&s_disconnectPending);
    ble_gap_conn_broken(1,534);CHECK(BleHid::connectionIdle()&&!s_advertisingAllowed);
    // A queued ADV_COMPLETE from an old invitation cannot close a new one.
    resetPair();CHECK(!BleHid::makeDiscoverable());processAdvertising();auto oldArg=fake::callbackArg;
    CHECK(!BleHid::disconnect());CHECK(BleHid::connectionIdle());
    CHECK(!BleHid::makeDiscoverable());processAdvertising();fake::advertising=false;
    late.type=BLE_GAP_EVENT_ADV_COMPLETE;gapEvent(&late,oldArg);
    CHECK(s_discoverableDeadlineUs>fake::now&&s_advAwaitingResult);
    // Cancellation while the old callback is cleaning up must not leave a
    // phantom disconnectPending flag after the handle becomes free.
    resetPair();openAndConnect();fake::onPairCleared=[](){CHECK(!BleHid::stopDiscoverable());CHECK(!BleHid::connectionIdle());};
    ble_gap_conn_broken(1,531);CHECK(BleHid::connectionIdle()&&!s_disconnectPending&&s_state==BleHidState::Ready);
    // Matches captured CONNECT -> SMP ENOTCONN -> DISCONNECT(0x213).
    for(unsigned target=0;target<2;++target){
        resetPair();openAndConnect(target);const auto deadline=s_discoverableDeadlineUs.load();
        CHECK(deadline>fake::now);CHECK(fake::securityRequests==0&&s_securityTimer.armed);
        CHECK(s_state==BleHidState::Connecting&&!s_advertisingAllowed);
        fake::now+=120000;ble_gap_conn_broken(1,BLE_HS_HCI_ERR(0x13));
        CHECK(fake::terminations==0&&s_pairError==0&&!s_securityTimer.armed);
        CHECK(s_connHandle==kInvalidHandle&&s_advertisingAllowed&&s_connectionAllowed);
        CHECK(s_discoverableDeadlineUs==deadline);if(target)CHECK(BlePeerStore::same(fake::active,phone()));
        processAdvertising();CHECK(fake::duration==59880);CHECK(fake::params.filter_policy==(target?2:0));
        CHECK(fake::keyboardAttempts==0);CHECK(BleHid::stopDiscoverable()==0);
        ble_gap_adv_stop();processAdvertising();CHECK(!s_connectionAllowed);
    }
    // Real reasons survive; real SMP failures do not reopen discovery.
    resetPair();openAndConnect();ble_gap_conn_broken(1,BLE_HS_HCI_ERR(8));
    CHECK(s_pairError==520&&s_state==BleHidState::Failed&&!s_advertisingAllowed&&fake::terminations==0);
    resetPair();openAndConnect();ble_gap_event fail{};fail.type=BLE_GAP_EVENT_ENC_CHANGE;fail.enc_change.status=BLE_HS_EAUTHEN;
    gapEvent(&fail,nullptr);ble_gap_conn_broken(1,531);
    CHECK(s_pairError==BLE_HS_EAUTHEN&&!s_advertisingAllowed&&fake::terminations==1);
    // Success ends invitation. A later remote disconnect never reconnects.
    resetPair();openAndConnect();encrypted();CHECK(s_state==BleHidState::Connected&&!s_securityTimer.armed&&s_privateEligible);
    CHECK(!s_discoverableDeadlineUs);ble_gap_conn_broken(1,531);processAdvertising();
    CHECK(s_state==BleHidState::Ready&&!s_advertisingAllowed&&fake::starts==1&&fake::terminations==0);
    // User cancel, Off, deadline, keyboard and stale events are separate paths.
    resetPair();openAndConnect();CHECK(!BleHid::stopDiscoverable());ble_gap_conn_broken(1,531);
    CHECK(!s_advertisingAllowed&&fake::terminations==1&&s_state==BleHidState::Ready);
    resetPair();openAndConnect();s_enabled=false;endConnectionWindow();ble_gap_conn_broken(1,531);
    CHECK(s_state==BleHidState::Disabled&&!s_advertisingAllowed);
    resetPair();openAndConnect();fake::now+=61000000;ble_gap_conn_broken(1,531);
    CHECK(!s_advertisingAllowed&&s_pairError==531);
    resetPair();openAndConnect(false,true);CHECK(fake::securityRequests==1&&!s_discoverableDeadlineUs);
    ble_gap_conn_broken(1,531);CHECK(!s_advertisingAllowed);
    resetPair();openAndConnect();s_securityTimer.cb(nullptr);CHECK(fake::securityRequests==1);
    resetPair();openAndConnect();encrypted();s_securityTimer.cb(nullptr);CHECK(fake::securityRequests==0);
    resetPair();openAndConnect();++s_linkGeneration;s_securityTimer.cb(nullptr);CHECK(fake::securityRequests==0);
    resetPair();openAndConnect();s_disconnectPending=true;s_securityTimer.cb(nullptr);CHECK(fake::securityRequests==0);
    resetPair();fake::expectedValid=false;openAndConnect();CHECK(fake::terminations==1&&fake::securityRequests==0&&!s_advertisingAllowed);
    resetPair();openAndConnect();ble_gap_event stale{};stale.type=BLE_GAP_EVENT_ENC_CHANGE;stale.enc_change.conn_handle=99;
    auto clears=fake::pairClears;gapEvent(&stale,nullptr);CHECK(clears==fake::pairClears);
    stale.type=BLE_GAP_EVENT_ADV_COMPLETE;gapEvent(&stale,nullptr);CHECK(s_discoverableDeadlineUs>0);
    // Worker can begin a different invitation the instant the slot is free.
    // The old disconnect must not replace its selected peer afterwards.
    resetPair();openAndConnect(true);
    fake::onHandleReleased=[](){CHECK(!BleHid::makeDiscoverable());s_state=BleHidState::Advertising;};
    ble_gap_conn_broken(1,531);
    CHECK(!s_targeted&&s_state==BleHidState::Advertising&&s_discoverable);
    CHECK(s_discoverableDeadlineUs==fake::now+60000000);
    std::printf("%u checks: real GAP callback and SDK teardown; ENOTCONN ordering, 60s invitation, target identity, cancellation, encrypted reconnect and delayed security; synthetic transport only\n",checks);
}
