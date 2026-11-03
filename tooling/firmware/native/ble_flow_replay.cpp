// Real worker flow; deterministic transport. Not a phone/PC radio acceptance test.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=259,ESP_ERR_NOT_FOUND=261,ESP_ERR_TIMEOUT=263;
#include "core/ble_connection_flow.hpp"
#include "core/system_service.hpp"
unsigned checks=0;
#define CHECK(v) do {++checks;if(!(v)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#v);std::abort();}}while(0)
namespace fake {
using namespace sys;
bool enabled=true,idle=true,connected=false,teardown=false;int failure=0;
BleHidState state=BleHidState::Ready;BleBond peer{},invited{};
std::vector<BleBond> bonds;
unsigned requests=0,disconnects=0,discoveries=0,cancels=0,seconds=0;
void release(){idle=true;connected=teardown=false;state=BleHidState::Ready;peer={};}
void reset(){release();enabled=true;failure=0;requests=disconnects=discoveries=cancels=seconds=0;invited={};}
}
namespace sys {
bool BlePeerStore::same(const BleBond&a,const BleBond&b){return (a.addrType&1)==(b.addrType&1)&&!std::memcmp(a.addr,b.addr,6);}
bool BleHid::enabled(){return fake::enabled;}bool BleHid::connected(){return fake::connected;}
bool BleHid::connectionIdle(){return fake::idle;}BleBond BleHid::peer(){return fake::peer;}
BleHidState BleHid::state(){return fake::state;}int BleHid::lastError(){return fake::failure;}
int BleHid::bonds(BleBond*out,int max){int n=0;for(auto b:fake::bonds){if(n==max)break;out[n++]=b;}return n;}
esp_err_t BleHid::disconnect(){++fake::disconnects;fake::teardown=!fake::idle;fake::connected=false;return ESP_OK;}
esp_err_t BleHid::stopDiscoverable(){++fake::cancels;fake::seconds=0;return ESP_OK;}
void BleHid::cancelUserScan(){}
uint32_t BleHid::discoverableSecondsRemaining(){return fake::seconds;}
esp_err_t BleHid::reconnectBond(const BleBond &b){
 CHECK(fake::idle);bool found=false;for(auto p:fake::bonds)if(BlePeerStore::same(p,b))found=true;
 if(!found)return ESP_ERR_NOT_FOUND;
 ++fake::requests;fake::invited=b;fake::idle=false;fake::state=BleHidState::Connecting;return ESP_OK;
}
esp_err_t BleHid::connectKeyboard(const BleScanDevice &d){return reconnectBond(d.identity);}
esp_err_t BleHid::makeDiscoverable(bool){CHECK(fake::idle);++fake::discoveries;fake::idle=false;fake::seconds=60;fake::state=BleHidState::Advertising;return ESP_OK;}
}
int main(){
 using namespace sys;BleBond a{},b{};a.addr[0]=1;b.addr[0]=2;fake::bonds={a,b};
 for(unsigned cycle=0;cycle<200;++cycle){
  fake::reset();BluetoothConnectionFlow flow;
  fake::peer=cycle%2?a:b;fake::connected=true;fake::idle=false;fake::state=BleHidState::Connected;
  const auto target=cycle%2?b:a;
  CHECK(flow.begin(BluetoothOperation::Saved,target,{},false,0)==ESP_OK);
  CHECK(flow.switching&&flow.operation==BluetoothOperation::Saved);
  for(int t=0;t<20;++t){flow.poll(t*100000,false);CHECK(fake::requests==0);}
  fake::release();flow.poll(2100000,false);CHECK(fake::requests==1&&BlePeerStore::same(fake::invited,target));
  CHECK(!flow.switching&&flow.operation==BluetoothOperation::Saved);
  // Uncompleted short connections do not change the user action.
  for(int t=0;t<20;++t){fake::state=t%2?BleHidState::Connecting:BleHidState::Advertising;flow.poll(2200000+t,false);CHECK(flow.operation==BluetoothOperation::Saved);}
  fake::connected=true;fake::peer=target;fake::state=BleHidState::Connected;flow.poll(3000000,false);
  CHECK(flow.operation==BluetoothOperation::None&&!flow.error);
  fake::release();flow.poll(90000000,false);CHECK(fake::requests==1); // no reconnect
 }
 // Cancel during teardown, shutdown, deleted target, latest selected target.
 for(unsigned mode=0;mode<5;++mode){
  fake::reset();BluetoothConnectionFlow flow;fake::idle=false;fake::state=BleHidState::Connected;
  CHECK(!flow.begin(BluetoothOperation::Saved,b,{},false,0));
  if(mode==0)flow.cancel();
  if(mode==1)flow.poll(10,true);
  if(mode==2){fake::enabled=false;flow.poll(10,false);}
  if(mode==3)fake::bonds={a};
  if(mode==4)CHECK(!flow.begin(BluetoothOperation::Saved,a,{},false,1));
  fake::release();flow.poll(100,false);
  CHECK(fake::requests==(mode==4?1u:0u));
  if(mode==3)CHECK(flow.error==ESP_ERR_NOT_FOUND);
  if(mode==4)CHECK(BlePeerStore::same(fake::invited,a));
  fake::bonds={a,b};
 }
 fake::reset();BluetoothConnectionFlow flow;
 CHECK(!flow.begin(BluetoothOperation::Discover,{}, {},false,0));flow.poll(1,false);
 for(unsigned t=0;t<100;++t){fake::state=t%2?BleHidState::Advertising:BleHidState::Connecting;flow.poll(100+t,false);CHECK(flow.operation==BluetoothOperation::Discover);
  BluetoothSnapshot ui;ui.enabled=true;ui.operation=flow.operation;ui.state=fake::state;
  CHECK(ui.discovering()&&ui.cancellable()&&!ui.hasError()&&!ui.connecting());
 }
 flow.cancel();fake::release();flow.poll(200,false);CHECK(fake::discoveries==1&&flow.operation==BluetoothOperation::None);
 fake::reset();fake::idle=false;CHECK(!flow.begin(BluetoothOperation::Saved,a,{},false,0));flow.poll(10000000,false);
 CHECK(flow.operation==BluetoothOperation::None&&flow.error==ESP_ERR_TIMEOUT&&!fake::requests);
 fake::reset();CHECK(!flow.begin(BluetoothOperation::Saved,a,{},false,0));flow.poll(1,false);
 fake::failure=23;fake::state=BleHidState::Failed;flow.poll(2,false);CHECK(flow.error==23&&!flow.switching&&flow.operation==BluetoothOperation::None);
 fake::bonds.clear();
 for(unsigned i=0;i<16;++i){auto p=a;p.addr[0]=uint8_t(i+1);p.keyboard=true;std::strcpy(p.name,"Controller");fake::bonds.push_back(p);}
 for(unsigned from=0;from<16;++from)for(unsigned to=0;to<16;++to)if(from!=to){
  fake::reset();BluetoothConnectionFlow next;fake::peer=fake::bonds[from];fake::connected=true;fake::idle=false;fake::state=BleHidState::Connected;
  CHECK(!next.begin(BluetoothOperation::Saved,fake::bonds[to],{},false,0));CHECK(next.switching);
  next.poll(1,false);CHECK(!fake::requests);fake::release();next.poll(2,false);
  CHECK(fake::requests==1&&BlePeerStore::same(fake::invited,fake::bonds[to]));
 }
 std::printf("%u checks: worker lifecycle, 200 alternating saved-host switches, all 240 sixteen-device switches, transient connection loops, cancellation, deleted targets, latest target, deadlines and shutdown; synthetic transport\n",checks);
}
