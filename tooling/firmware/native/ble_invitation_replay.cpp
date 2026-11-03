// Production request / advertisement functions; synthetic NimBLE transport.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "core/ble_hid.hpp"
#include "core/ble_peer_store.hpp"
#include "core/ble_peer_identity.hpp"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=259,ESP_ERR_NOT_FOUND=261;
constexpr int BLE_HS_EINVAL=3,BLE_HS_EALREADY=2;
constexpr int BLE_ERR_REM_USER_CONN_TERM=0x13;
constexpr int BLE_SM_IO_CAP_NO_IO=3,BLE_SM_IO_CAP_DISP_YES_NO=1;
constexpr uint8_t BLE_OWN_ADDR_PUBLIC=0,BLE_HS_ADV_F_BREDR_UNSUP=4,BLE_HS_ADV_F_DISC_GEN=2;
constexpr uint8_t BLE_GAP_CONN_MODE_UND=2,BLE_GAP_DISC_MODE_GEN=2,BLE_GAP_DISC_MODE_NON=0;
constexpr uint8_t BLE_HCI_ADV_FILT_CONN=2,BLE_HCI_ADV_FILT_NONE=0;
constexpr uint16_t ESP_HID_APPEARANCE_KEYBOARD=0x3c1;
struct ble_addr_t {uint8_t type=0,val[6]{};};
struct ble_uuid16_t {uint16_t value;};
struct ble_hs_adv_fields {
    uint8_t flags=0,*name=nullptr,name_len=0,name_is_complete=0;
    uint16_t appearance=0;uint8_t appearance_is_present=0;
    ble_uuid16_t *uuids16=nullptr;uint8_t num_uuids16=0,uuids16_is_complete=0;
};
struct ble_gap_adv_params {uint8_t conn_mode=0,disc_mode=0,high_duty_cycle=0,filter_policy=0;uint16_t itvl_min=0,itvl_max=0;};
struct ble_gap_event{};
struct {int sm_io_cap=0,sm_mitm=0;} ble_hs_cfg;
namespace fake {
int64_t now=1000000;bool synced=true,advertising=false,scan=false;
int inferError=0,allowError=0,advError=0,responseError=0,startError=0;
unsigned starts=0,allowCalls=0,stops=0,keyboardAttempts=0;
unsigned terminations=0;
ble_addr_t selected{};ble_gap_adv_params params{};int32_t duration=0;
std::vector<uint8_t> primary,response;std::vector<sys::BleBond> bonds;
sys::BleBond active{};
void *callbackArg=nullptr;
}
unsigned checks=0;
#define CHECK(v) do {++checks;if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::abort();}}while(0)
int64_t esp_timer_get_time(){return fake::now;}
bool ble_hs_synced(){return fake::synced;}
bool ble_gap_adv_active(){return fake::advertising;}
int ble_gap_adv_stop(){fake::advertising=false;++fake::stops;return 0;}
int ble_gap_terminate(uint16_t,int){++fake::terminations;return 0;}
int ble_gap_conn_cancel(){return 0;}
int ble_hs_id_infer_auto(int,uint8_t *out){*out=0;return fake::inferError;}
bool ble_gap_rpa_resolve(uint8_t *rpa,uint8_t *out,uint8_t *type){
    if(rpa[0]!=42)return false;*type=0;const uint8_t id[]={1,2,3,4,5,6};std::memcpy(out,id,6);return true;
}
int ble_gap_wl_set(const ble_addr_t *ids,uint8_t count){CHECK(count==1);++fake::allowCalls;fake::selected=*ids;return fake::allowError;}
std::vector<uint8_t> encode(const ble_hs_adv_fields &f){
    std::vector<uint8_t> b;
    auto field=[&](uint8_t type,std::vector<uint8_t> data){b.push_back(uint8_t(data.size()+1));b.push_back(type);b.insert(b.end(),data.begin(),data.end());};
    if(f.flags)field(1,{f.flags});
    if(f.num_uuids16){CHECK(f.num_uuids16==1);field(f.uuids16_is_complete?3:2,{uint8_t(f.uuids16->value),uint8_t(f.uuids16->value>>8)});}
    if(f.appearance_is_present)field(0x19,{uint8_t(f.appearance),uint8_t(f.appearance>>8)});
    if(f.name_len)field(f.name_is_complete?9:8,{f.name,f.name+f.name_len});
    CHECK(b.size()<=31);return b;
}
int ble_gap_adv_set_fields(const ble_hs_adv_fields *f){fake::primary=encode(*f);return fake::advError;}
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields *f){fake::response=encode(*f);return fake::responseError;}
int ble_gap_adv_start(uint8_t,const ble_addr_t *direct,int32_t duration,const ble_gap_adv_params *params,int(*)(ble_gap_event*,void*),void *arg){
    fake::callbackArg=arg;
    CHECK(direct==nullptr);++fake::starts;fake::params=*params;fake::duration=duration;
    if(!fake::startError)fake::advertising=true;return fake::startError;
}
namespace sys {
constexpr char kTag[]="ble_hid",kDeviceName[]="Brookesia-s3";
constexpr uint16_t kInvalidHandle=0xffff;
#include "bond_capacity.inc"
constexpr int64_t kDiscoverableDurationUs=60000000;
std::recursive_mutex s_scanControlMutex;
std::atomic<bool> s_enabled{true},s_initialized{true},s_connectPending{false},s_presenceScan{false};
std::atomic<bool> s_connectionAllowed{false},s_advertisingAllowed{false},s_targeted{false},s_discoverable{false};
std::atomic<bool> s_advPending{false},s_advRestart{false},s_requireAuthenticated{false};
std::atomic<bool> s_disconnectPending{false};
std::atomic<bool> s_advAwaitingResult{false},s_gapTeardown{false};std::atomic<uintptr_t> s_advGeneration{0};
std::atomic<int64_t> s_discoverableDeadlineUs{0};std::atomic<uint16_t> s_connHandle{kInvalidHandle};
std::atomic<uint8_t> s_ownAddrType{0};std::atomic<int> s_lastError{0},s_pairError{0};
std::atomic<BleHidState> s_state{BleHidState::Ready};
ble_hs_adv_fields s_advFields;ble_uuid16_t s_hidUuid{0x1812};
int gapEvent(ble_gap_event *,void*){return 0;}
void BleHid::stopScan(){fake::scan=false;s_presenceScan=false;}
int BleHid::bonds(BleBond *out,int max){int n=std::min<int>(max,fake::bonds.size());std::copy_n(fake::bonds.begin(),n,out);return n;}
esp_err_t BleHid::reconnectKeyboard(const BleBond &value){++fake::keyboardAttempts;fake::active=value;return 0;}
bool BlePeerStore::same(const BleBond&a,const BleBond&b){return (a.addrType&1)==(b.addrType&1)&&!std::memcmp(a.addr,b.addr,6);}
void BlePeerStore::activate(const BleBond &b){fake::active=b;}
BleBond BlePeerStore::active(){return fake::active;}
void BlePeerStore::clearActive(){fake::active={};}
#include "adv_config.inc"
#include "invitation.inc"
}
sys::BleBond phone(){sys::BleBond b;const uint8_t id[]={1,2,3,4,5,6};std::memcpy(b.addr,id,6);return b;}
void reset(){
    using namespace sys;
    fake::now=1000000;fake::synced=true;fake::advertising=false;fake::scan=false;
    fake::inferError=fake::allowError=fake::advError=fake::responseError=fake::startError=0;
    fake::starts=fake::allowCalls=fake::stops=fake::keyboardAttempts=0;fake::bonds={phone()};
    fake::primary.clear();fake::response.clear();fake::selected={};fake::active={};
    s_enabled=true;s_initialized=true;s_connectPending=false;s_presenceScan=false;s_connHandle=kInvalidHandle;
    s_disconnectPending=false;s_advAwaitingResult=false;fake::terminations=0;
    endConnectionWindow();s_targeted=false;s_state=BleHidState::Ready;s_lastError=0;s_pairError=0;configureGap();
}
int main(){
    using namespace sys;
    for(unsigned i=0;i<200;++i){
        reset();fake::scan=true;
        CHECK(BleHid::reconnectBond(phone())==ESP_OK);CHECK(!fake::scan);CHECK(s_targeted&&s_connectionAllowed);
        processAdvertising();CHECK(fake::starts==1&&fake::allowCalls==1&&fake::advertising);
        CHECK(fake::selected.type==0&&!std::memcmp(fake::selected.val,phone().addr,6));
        CHECK(fake::params.filter_policy==BLE_HCI_ADV_FILT_CONN);CHECK(fake::params.conn_mode==BLE_GAP_CONN_MODE_UND);
        CHECK(fake::params.itvl_min==32&&fake::params.itvl_max==32);CHECK(fake::duration==60000);
        CHECK(fake::primary==std::vector<uint8_t>({2,1,6,3,3,0x12,0x18,3,0x19,0xc1,3}));
        CHECK(fake::response.size()==sizeof(kDeviceName)+1&&fake::response[1]==9);
        CHECK(std::string(fake::response.begin()+2,fake::response.end())==kDeviceName);
        processAdvertising();CHECK(fake::starts==1); // no implicit restart
        CHECK(BleHid::stopDiscoverable()==ESP_OK);CHECK(!s_connectionAllowed&&!s_advertisingAllowed);
        ble_gap_adv_stop();processAdvertising();CHECK(fake::starts==1);
        CHECK(BleHid::makeDiscoverable()==ESP_OK);CHECK(!s_targeted);processAdvertising();
        CHECK(fake::starts==2&&fake::params.filter_policy==BLE_HCI_ADV_FILT_NONE);CHECK(fake::allowCalls==1);
        CHECK(s_state==BleHidState::Advertising);CHECK(!s_requireAuthenticated);
        // Unknown/deleted records never become an invitation by stale UI index.
        reset();fake::bonds.clear();CHECK(BleHid::reconnectBond(phone())==ESP_ERR_NOT_FOUND);CHECK(!s_advertisingAllowed);
        reset();auto keyboard=phone();keyboard.keyboard=true;fake::bonds={keyboard};
        CHECK(BleHid::reconnectBond(keyboard)==ESP_OK);CHECK(fake::keyboardAttempts==1&&fake::starts==0);
        // An RPA is authenticated by the supplied resolver, never by its name.
        reset();auto rpa=phone();rpa.addrType=1;rpa.addr[0]=42;rpa.addr[5]=0x40;fake::bonds={rpa};
        CHECK(BleHid::reconnectBond(rpa)==ESP_OK);processAdvertising();CHECK(fake::selected.type==0&&fake::selected.val[0]==1);
        reset();rpa.addr[0]=99;fake::bonds={rpa};CHECK(BleHid::reconnectBond(rpa)==ESP_OK);processAdvertising();
        CHECK(fake::starts==0&&!s_connectionAllowed&&s_state==BleHidState::Failed);
        // Fail closed on each preparation stage; do not silently invite anyone.
        for(unsigned stage=0;stage<5;++stage){
            reset();CHECK(BleHid::reconnectBond(phone())==ESP_OK);
            int *error[]={&fake::inferError,&fake::allowError,&fake::advError,&fake::responseError,&fake::startError};*error[stage]=7;
            processAdvertising();CHECK(!s_connectionAllowed&&!s_advertisingAllowed&&s_state==BleHidState::Failed&&s_lastError==7);
            unsigned starts=fake::starts;processAdvertising();CHECK(fake::starts==starts);
        }
        reset();CHECK(!BleHid::makeDiscoverable());fake::now+=60000000;processAdvertising();CHECK(fake::starts==0&&!s_advertisingAllowed);
        reset();s_enabled=false;CHECK(BleHid::makeDiscoverable()==ESP_ERR_INVALID_STATE);processAdvertising();CHECK(!fake::starts);
        reset();s_connHandle=7;CHECK(BleHid::makeDiscoverable()==ESP_ERR_INVALID_STATE);CHECK(BleHid::reconnectBond(phone())==ESP_ERR_INVALID_STATE);
        reset();CHECK(!BleHid::makeDiscoverable(true));CHECK(s_requireAuthenticated&&ble_hs_cfg.sm_mitm==1);
    }
    reset();CHECK(kMaxBondSlots==16);fake::bonds.clear();
    for(unsigned i=0;i<16;++i){auto p=phone();p.addr[0]=uint8_t(i+1);p.keyboard=true;std::strcpy(p.name,"Controller");fake::bonds.push_back(p);}
    for(const auto &p:fake::bonds){CHECK(BleHid::reconnectBond(p)==ESP_OK);CHECK(BlePeerStore::same(fake::active,p));}
    std::printf("%u checks; 200 invitation cycles and sixteen saved identities: saved-peer filter, iPhone discovery data, RPA identity, cancellation, 60s deadline, keyboard delegation and injected HCI failures; fake radio only\n",checks);
}
