#include <atomic>
#include <mutex>
#include <string>
#include "flow.inc"
#include "core/connectivity.hpp"
constexpr int ESP_ERR_NO_MEM=257,pdTRUE=1;
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct ble_addr_t {uint8_t type=0,val[6]{};};
struct ble_store_value_sec {ble_addr_t peer_addr;uint16_t bond_count=0;bool ltk_present=true;};
union ble_store_value {ble_store_value_sec sec;};
constexpr int BLE_STORE_OBJ_TYPE_OUR_SEC=1;
namespace bootfake {
int64_t now=1000000;bool sleeping=false,standby=false,hotspot=false,queueOK=true;
int wifiError=0,bleError=0,connectError=0;unsigned wifiConnects=0;
sys::WifiSnapshot wifi;sys::BleBond target;
std::string savedSsid="last-success",requestedSsid;
std::vector<sys::ServiceAction> enabled;
std::vector<ble_store_value_sec> records;
int storeError=0;
}
int ble_store_iterate(int kind,int (*cb)(int,ble_store_value*,void*),void *cookie){
 CHECK(kind==BLE_STORE_OBJ_TYPE_OUR_SEC);
 for(auto record:bootfake::records){ble_store_value value{record};CHECK(cb(kind,&value,cookie)==0);}
 return bootfake::storeError;
}
int64_t esp_timer_get_time(){return bootfake::now;}
namespace sys {
struct Power {static bool sleepPending(){return bootfake::sleeping;}static bool inStandby(){return bootfake::standby;}};
struct Settings {
 static bool wifiOn(){return true;}static bool bluetoothOn(){return true;}static void setBluetoothOn(bool){}
 static std::string wifiSsid(){return bootfake::savedSsid;}static std::string wifiPassword(){return "synthetic-only";}
};
bool Connectivity::wifiEnabled(){return bootfake::wifi.enabled;}
bool Connectivity::hotspotReserved(){return bootfake::hotspot;}
WifiSnapshot Connectivity::snapshot(){return bootfake::wifi;}
esp_err_t Connectivity::connectWifi(const std::string &s,const std::string &){++bootfake::wifiConnects;bootfake::requestedSsid=s;return bootfake::connectError;}
BleBond BlePeerStore::target(){return bootfake::target;}
BleBond BlePeerStore::enrich(BleBond b){for(const auto &p:fake::bonds)if(same(p,b))return p;return b;}
std::atomic<bool> s_initialized{true};
#include "boot_bond.inc"
struct Command {ServiceAction action;bool value=false;char password[65]{};uint32_t connectionGeneration=0;};
std::mutex snapshotMutex;ServiceResult results[3];
std::atomic<bool> started{true};std::atomic<uint32_t> connectionGeneration{0};std::atomic<unsigned> pending[3]{};
int queue=0;int xQueueSend(int,const Command*,int){return bootfake::queueOK?pdTRUE:0;}
ServiceDomain SystemService::domain(ServiceAction a){if(a<=ServiceAction::WifiForget)return ServiceDomain::Wifi;if(a<=ServiceAction::PairAnswer)return ServiceDomain::Bluetooth;return ServiceDomain::System;}
void SystemService::wake(){}
esp_err_t execute(const Command &c){
 bootfake::enabled.push_back(c.action);
 if(c.action==ServiceAction::WifiEnable){bootfake::wifi.enabled=!bootfake::wifiError;return bootfake::wifiError;}
 if(c.action==ServiceAction::BluetoothEnable){fake::enabled=!bootfake::bleError;return bootfake::bleError;}
 std::abort();
}
#include "boot_state.inc"
#include "enqueue.inc"
#include "boot.inc"
}
void resetBoot(){
 using namespace sys;fake::reset();bootfake::now=1000000;bootfake::sleeping=bootfake::standby=bootfake::hotspot=false;
 bootfake::queueOK=true;bootfake::wifiError=bootfake::bleError=bootfake::connectError=0;
 bootfake::wifiConnects=0;bootfake::enabled.clear();bootfake::wifi={};bootfake::wifi.state=WifiState::Disconnected;
 bootfake::wifi.transitioning=true;bootfake::savedSsid="last-success";bootfake::requestedSsid.clear();
 bootfake::target={};bootfake::target.addr[0]=2;BleBond a{};a.addr[0]=1;fake::bonds={a,bootfake::target};
 bootfake::records={{{0,{2}},20,true},{{0,{1}},10,true}};bootfake::storeError=0;s_initialized=true;
 fake::state=BleHidState::Starting;bluetoothFlow={};bootWifiAllowed=bootBluetoothAllowed=true;
 bootWifiPending=bootBluetoothPending=false;bootConnectDeadline=0;
}
int main(){using namespace sys;
 // Delayed readiness never starts a second attempt or changes target to the first bond.
 resetBoot();startBootRadios();CHECK(bootfake::enabled==std::vector<ServiceAction>({ServiceAction::WifiEnable,ServiceAction::BluetoothEnable}));
 for(int i=0;i<50;++i){pollBootConnections();CHECK(!bootfake::wifiConnects&&!fake::requests);}
 bootfake::wifi.transitioning=false;fake::state=BleHidState::Ready;pollBootConnections();bluetoothFlow.poll(bootfake::now,false);
 CHECK(bootfake::wifiConnects==1&&bootfake::requestedSsid=="last-success"&&fake::requests==1&&fake::invited.addr[0]==2);
 fake::release();bluetoothFlow.cancel();for(int i=0;i<100;++i)pollBootConnections();CHECK(bootfake::wifiConnects==1&&fake::requests==1);
 // Explicit commands accepted before host readiness supersede only that radio's boot intent.
 for(auto a:{ServiceAction::WifiEnable,ServiceAction::WifiConnect,ServiceAction::WifiDisconnect,ServiceAction::WifiScan,ServiceAction::WifiForget,
             ServiceAction::BluetoothEnable,ServiceAction::BluetoothDiscover,ServiceAction::BluetoothDisconnect,ServiceAction::BluetoothScan,ServiceAction::BluetoothConnect,ServiceAction::BluetoothReconnect,ServiceAction::BluetoothForget,ServiceAction::BluetoothForgetAll}){
  resetBoot();startBootRadios();CHECK(!enqueue({a}));bootfake::wifi.transitioning=false;fake::state=BleHidState::Ready;
  pollBootConnections();bluetoothFlow.poll(bootfake::now,false);
  CHECK(bootfake::wifiConnects==(a<=ServiceAction::WifiForget?0u:1u));CHECK(fake::requests==(a<=ServiceAction::WifiForget?1u:0u));
 }
 resetBoot();CHECK(!enqueue({ServiceAction::WifiEnable,false}));CHECK(!enqueue({ServiceAction::BluetoothEnable,false}));startBootRadios();CHECK(bootfake::enabled.empty());
 resetBoot();startBootRadios();SystemService::cancelBluetoothConnection();fake::state=BleHidState::Ready;pollBootConnections();CHECK(!bootBluetoothPending&&bluetoothFlow.operation==BluetoothOperation::None);
 // Failure, sleep, hotspot handoff, deadline and no saved history must be terminal.
 for(int mode=0;mode<7;++mode){resetBoot();startBootRadios();
  if(mode==0)bootfake::sleeping=true;
  if(mode==1)bootfake::standby=true;
  if(mode==2)bootfake::now=bootConnectDeadline;
  if(mode==3){bootfake::wifi.enabled=false;fake::enabled=false;}
  if(mode==4){bootfake::hotspot=true;fake::state=BleHidState::Failed;}
  if(mode==5){bootfake::savedSsid.clear();bootfake::records.clear();fake::bonds.clear();}
  if(mode==6){bootfake::savedSsid.clear();bootfake::storeError=1;} // failed read cannot use a partial selection
  bootfake::wifi.transitioning=false;if(mode!=4)fake::state=BleHidState::Ready;
  pollBootConnections();CHECK(!bootWifiPending&&!bootBluetoothPending&&!bootfake::wifiConnects&&bluetoothFlow.operation==BluetoothOperation::None);
  bootfake::sleeping=bootfake::standby=bootfake::hotspot=false;pollBootConnections();CHECK(!bootfake::wifiConnects);
 }
 resetBoot();bootfake::wifiError=ESP_ERR_NO_MEM;startBootRadios();CHECK(!bootWifiPending&&bootBluetoothPending&&bootfake::enabled.size()==2);
 resetBoot();bootfake::bleError=ESP_FAIL;startBootRadios();CHECK(bootWifiPending&&!bootBluetoothPending);
 resetBoot();bootfake::target={};fake::bonds.resize(1);bootfake::records.erase(bootfake::records.begin());startBootRadios();fake::state=BleHidState::Ready;pollBootConnections();bluetoothFlow.poll(bootfake::now,false);CHECK(fake::requests==1&&fake::invited.addr[0]==1);
 // Reproduce the device: newer paired PC=2, last successful connection Mac=1.
 // A valid recent connection wins; missing/forgotten metadata falls back to pairing order.
 for(int old:{1,0,9}){resetBoot();bootfake::target.addr[0]=old;startBootRadios();fake::state=BleHidState::Ready;pollBootConnections();bluetoothFlow.poll(bootfake::now,false);CHECK(fake::requests==1&&fake::invited.addr[0]==(old==1?1:2));}
 // SDK counter normalization preserves the fallback order; enumeration order is irrelevant.
 for(int counter:{1,33,65520})for(int reverse=0;reverse<2;++reverse){resetBoot();
  bootfake::target={};
  bootfake::records[0].bond_count=counter+1;bootfake::records[1].bond_count=counter;
  if(reverse)std::swap(bootfake::records[0],bootfake::records[1]);
  BleBond selected{};CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==2);
 }
 resetBoot();BleBond selected{};bootfake::records[0].ltk_present=false;
 CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==2); // keyboard can keep its key in PEER_SEC
 bootfake::records[0].peer_addr.type=255;CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==1);
 bootfake::records[1].peer_addr={};CHECK(BleHid::bootBond(selected)==ESP_ERR_NOT_FOUND&&selected.addr[0]==0);
 resetBoot();bootfake::records[0].peer_addr.type=2;CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addrType==0);
 s_initialized=false;CHECK(BleHid::bootBond(selected)==ESP_ERR_INVALID_STATE&&selected.addr[0]==0);
 // Preference uses the full identity, not an address with a different type or a reused name.
 for(int type:{0,1,2,3,255}){resetBoot();bootfake::target.addr[0]=1;bootfake::target.addrType=type;
  CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==((type==0||type==2)?1:2));
 }
 resetBoot();bootfake::target.addr[0]=9;std::strcpy(bootfake::target.name,"same name");
 for(auto &peer:fake::bonds)std::strcpy(peer.name,"same name");
 CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==2);
 // Enrich from the matched bond metadata, never from a stale target's name/role.
 for(bool keyboard:{false,true})for(int reverse=0;reverse<2;++reverse){resetBoot();
  bootfake::target.addr[0]=1;bootfake::target.keyboard=!keyboard;std::strcpy(bootfake::target.name,"stale");
  fake::bonds[0].keyboard=keyboard;std::strcpy(fake::bonds[0].name,"current");
  if(reverse)std::swap(bootfake::records[0],bootfake::records[1]);
  CHECK(BleHid::bootBond(selected)==ESP_OK&&selected.addr[0]==1&&selected.keyboard==keyboard&&!std::strcmp(selected.name,"current"));
  startBootRadios();fake::state=BleHidState::Ready;pollBootConnections();bluetoothFlow.poll(bootfake::now,false);
  CHECK(fake::requests==1&&fake::invited.addr[0]==1&&fake::invited.keyboard==keyboard);
 }
 // Even a matched preference cannot escape an iteration error or authorize a removed bond.
 resetBoot();bootfake::target.addr[0]=1;bootfake::storeError=1;
 CHECK(BleHid::bootBond(selected)==ESP_FAIL&&selected.addr[0]==0);
 resetBoot();bootfake::target.addr[0]=1;startBootRadios();fake::state=BleHidState::Ready;pollBootConnections();
 fake::bonds.erase(fake::bonds.begin());bluetoothFlow.poll(bootfake::now,false);
 CHECK(!fake::requests&&bluetoothFlow.error==ESP_ERR_NOT_FOUND);
 // One exhausted invitation reports timeout; the scheduler never starts another target.
 resetBoot();bootfake::target.addr[0]=1;startBootRadios();fake::state=BleHidState::Ready;pollBootConnections();bluetoothFlow.poll(bootfake::now,false);
 CHECK(fake::requests==1&&fake::invited.addr[0]==1);fake::release();bluetoothFlow.poll(bootfake::now+60000000,false);
 CHECK(bluetoothFlow.error==ESP_ERR_TIMEOUT);pollBootConnections();CHECK(fake::requests==1);
 resetBoot();startBootRadios();bootfake::wifi.transitioning=false;bootfake::connectError=ESP_FAIL;pollBootConnections();pollBootConnections();CHECK(bootfake::wifiConnects==1);
 std::printf("%u checks: production boot scheduler, last-success preference, bonded-identity validation, pairing-order fallback, saved-peer flow, delayed readiness, user priority, sleep, store errors, stale metadata, role preservation, timeout and one-shot consumption\n",checks);
}
