// Replay the production scan callback with a crowded synthetic BLE environment.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "core/ble_hid.hpp"
using namespace sys;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
constexpr int BLE_GAP_EVENT_DISC_COMPLETE=1,BLE_GAP_EVENT_DISC=2;
constexpr uint8_t BLE_HCI_ADV_RPT_EVTYPE_ADV_IND=0,BLE_HCI_ADV_RPT_EVTYPE_DIR_IND=1;
struct ble_addr_t {uint8_t type,val[6];};
struct ble_uuid16_t {struct {uint16_t value;}u;};
static uint16_t ble_uuid_u16(const decltype(ble_uuid16_t::u)*u){return u->value;}
struct ble_hs_adv_fields {const uint8_t *name=nullptr;size_t name_len=0;ble_uuid16_t *uuids16=nullptr;unsigned num_uuids16=0;uint16_t appearance=0;bool appearance_is_present=false;};
static ble_hs_adv_fields fields;
struct ble_gap_event {int type=0;struct{int reason;}disc_complete{};struct{ble_addr_t addr{};int rssi=-40;uint8_t event_type=0;const void *data=nullptr;size_t length_data=0;}disc;};
static int ble_hs_adv_parse_fields(ble_hs_adv_fields *f,const void*,size_t){*f=fields;return 0;}
static int64_t esp_timer_get_time(){return 3000000;}
static int ble_gap_rpa_resolve=0;
namespace sys::ble {static bool resolveIdentity(const ble_addr_t&,ble_addr_t&,int){return false;}}
static BleBond target;
namespace sys {class BlePeerStore {public:static BleBond target(){return ::target;}static bool same(const BleBond&a,const BleBond&b){return a.addrType==b.addrType&&!std::memcmp(a.addr,b.addr,6);}};}
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static int s_scanLock=0,s_scanCount=0;
static uintptr_t s_scanGeneration=1;
static std::atomic<BleScanState> s_scanState{BleScanState::Scanning};
static BleScanDevice s_scanDevices[BleHid::kMaxScanDevices];
#include "scan.inc"
static void emit(unsigned id,const char *name="",bool hid=false,uint8_t kind=0,uintptr_t generation=1){
 fields={};fields.name=reinterpret_cast<const uint8_t*>(name);fields.name_len=std::strlen(name);
 ble_uuid16_t uuid{{0x1812}};if(hid){fields.uuids16=&uuid;fields.num_uuids16=1;}
 ble_gap_event e;e.type=BLE_GAP_EVENT_DISC;e.disc.addr.val[0]=id;e.disc.event_type=kind;
 scanEvent(&e,reinterpret_cast<void*>(generation));
}
static int find(unsigned id){for(int i=0;i<s_scanCount;++i)if(s_scanDevices[i].address.addr[0]==id)return i;return -1;}
int main(){
 for(unsigned i=1;i<=BleHid::kMaxScanDevices;++i)emit(i,"",false,3);
 CHECK(s_scanCount==BleHid::kMaxScanDevices);
 emit(100,"IINE-keyboard",true);CHECK(find(100)>=0);auto &pad=s_scanDevices[find(100)];CHECK(pad.hid&&pad.connectable&&!std::strcmp(pad.name,"IINE-keyboard"));
 const int slot=find(100);emit(100,"",false,4);CHECK(find(100)==slot&&pad.hid&&pad.connectable&&pad.name[0]);
 emit(101,"keyboard",false);CHECK(find(101)>=0);
 // HID metadata in a scan response can evict a low priority record. The
 // next primary advertisement restores connectability without losing name.
 emit(102,"IINE-keyboard",true,4);CHECK(find(102)>=0&&!s_scanDevices[find(102)].connectable);
 emit(102);CHECK(s_scanDevices[find(102)].connectable&&s_scanDevices[find(102)].hid);
 target=s_scanDevices[find(101)].identity;target.keyboard=true;
 for(unsigned i=110;i<180;++i)emit(i,"HID",true);
 CHECK(find(100)>=0&&find(101)>=0&&find(102)>=0&&s_scanCount==BleHid::kMaxScanDevices);
 auto previous=s_scanDevices[0];emit(200,"stale",true,0,2);CHECK(find(200)==-1&&BlePeerStore::same(previous.identity,s_scanDevices[0].identity));
 s_scanState=BleScanState::Idle;emit(201,"cancelled",true);CHECK(find(201)==-1);
 s_scanState=BleScanState::Scanning;
 ble_gap_event end;end.type=BLE_GAP_EVENT_DISC_COMPLETE;scanEvent(&end,reinterpret_cast<void*>(2));CHECK(s_scanState==BleScanState::Scanning);
 scanEvent(&end,reinterpret_cast<void*>(1));CHECK(s_scanState==BleScanState::Complete);
 std::printf("%u checks; actual scan callback: full table, late HID, split advertising, selected target, stable rows and stale/cancelled callbacks passed\n",checks);
}
