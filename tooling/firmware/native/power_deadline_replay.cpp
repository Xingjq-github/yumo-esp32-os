// Production GUI policy/runtime, with hardware and running sessions injected.
#include "core/power_mgr.hpp"
#include "core/power_policy.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fixture {
sys::power::Config config;
bool lit=true,pending=false,busy=false,mouseBusy=false,radioBusy=false,connected=false,supply=false;
bool automaticDark=false;
bool economy=false,light=false;unsigned deep=0,radioOff=0;int64_t now=0;
}
int64_t esp_timer_get_time(){return fixture::now*1000;}
void bsp_power_wake(){}
void bsp_power_set_quiet(bool,bool){}
namespace core {
namespace xiaozhi {bool idle(){return !fixture::busy;}}
namespace mp3 {bool idle(){return !fixture::busy;}}
namespace speed {bool idle(){return !fixture::busy;}}
namespace chip {bool busy(){return fixture::busy;}}
namespace game_audio {bool busy(){return fixture::busy;}}
namespace photo {bool idle(){return !fixture::busy;}}
namespace sd {bool busy(){return fixture::busy;}}
namespace recorder {bool idle(){return !fixture::busy;}}
namespace fc {bool idle(){return !fixture::busy;}}
bool storageIdle(){return !fixture::busy;}
}
namespace sys {
namespace vault {bool idle(){return !fixture::busy;}}
enum class WifiState {Disconnected,Searching,Connecting,Connected};
enum class BleHidState {Ready,Starting,Connecting};
enum class BleScanState {Idle,Scanning};
enum class BlePairAction {None,Confirm};
enum class ServiceDomain {Wifi,Bluetooth,System};
enum class EventType {BluetoothToggled};
struct Event {EventType type;int value;};
struct EventBus {static void publish(Event){}};
struct Wifi {bool enabled=true,transitioning=false,scanPending=false;WifiState state=WifiState::Disconnected;};
struct Bt {BleHidState state=BleHidState::Ready;BleScanState scan=BleScanState::Idle;BlePairAction pairing=BlePairAction::None;};
struct Connectivity {
 static Wifi snapshot(){Wifi w;w.transitioning=fixture::radioBusy;w.state=wifiState();return w;}
 static WifiState wifiState(){return fixture::connected?WifiState::Connected:WifiState::Disconnected;}
 static int setWifiEnabled(bool on){CHECK(!on);++fixture::radioOff;return 0;}
};
struct BleHid {
 static bool keysIdle(){return !fixture::busy;}static bool mouseIdle(){return !fixture::mouseBusy;}static bool privateInputIdle(){return !fixture::busy;}
 static bool connected(){return fixture::connected;}static bool enabled(){return true;}
 static int setEnabled(bool on){CHECK(!on);++fixture::radioOff;return 0;}
};
struct SystemService {
 static void wake(){}static bool busy(ServiceDomain){return fixture::busy;}
 static bool timeSyncBusy(){return fixture::busy;}static Bt bluetooth(){return {};}
};
struct Settings {static power::Config powerConfig(){return fixture::config;}static void setBluetoothOn(bool){}};
struct Battery {static void pollSupply(){}static bool supplyProtected(){return fixture::supply;}};
struct UsbStorage {static bool idle(){return !fixture::busy;}};
struct Screenshot {static bool idle(){return !fixture::busy;}};
struct DisplayCast {static bool idle(){return !fixture::busy;}static bool enabled(){return fixture::busy;}};
struct LanHotspot {static bool idle(){return !fixture::busy;}};
struct Audio {static bool releaseIdle(){return true;}};
bool Power::isScreenOn(){return fixture::lit;}
bool Power::sleepPending(){return fixture::pending;}
void Power::setEconomy(bool low,bool,bool light){fixture::economy=low;fixture::light=light;}
void Power::automaticScreenOff(){CHECK(automaticScreenOffAllowed());fixture::lit=false;fixture::automaticDark=true;}
void Power::restoreAutomaticScreen(){if(fixture::automaticDark&&!fixture::pending){fixture::lit=true;fixture::automaticDark=false;}}
esp_err_t Power::deepSleep(bool automatic){CHECK(automatic&&!fixture::lit);++fixture::deep;fixture::pending=true;return 0;}
}
#include "power_runtime.inc"
void reset(){
 fixture::config={};fixture::lit=true;fixture::pending=fixture::busy=fixture::mouseBusy=fixture::radioBusy=fixture::connected=fixture::supply=false;
 fixture::economy=fixture::light=false;fixture::deep=fixture::radioOff=0;fixture::now=0;
 fixture::automaticDark=false;
 sys::policy.reset();sys::activitySerial=1;sys::idleSerial=0;sys::quiet=sys::slow=sys::stopRadios=false;
}
int main(){using namespace sys;
 reset();CHECK(!jobsBusy());fixture::mouseBusy=true;CHECK(jobsBusy());fixture::mouseBusy=false;CHECK(!jobsBusy());
 for(unsigned mask=0;mask<64;++mask){
  reset();fixture::lit=false;fixture::config.sleepOnPower=1;
  fixture::supply=mask&1;fixture::connected=mask&2;fixture::busy=mask&4;fixture::radioBusy=mask&8;
  const unsigned hints=(mask&16)?power::StayLit|power::KeepSession|power::LowLoad:power::None;
  Power::pollAutomatic(hints);CHECK(!fixture::deep&&Power::automaticSleepAllowed());
  fixture::now=119999;Power::pollAutomatic(hints);CHECK(!fixture::economy&&!fixture::deep);
  fixture::now=120000;Power::pollAutomatic(hints);CHECK(fixture::economy&&Power::slowIdle()&&!fixture::deep);
  Power::processEconomy();CHECK(!fixture::radioOff); // standby preserves links until deep sleep
  if(mask&32){Power::activity();fixture::supply=!fixture::supply;fixture::config.brightness=35;}
  fixture::now=239999;Power::pollAutomatic(hints^power::Dim);CHECK(!fixture::deep);
  fixture::now=240000;Power::pollAutomatic(hints);CHECK(fixture::deep==1&&fixture::pending);
  Power::pollAutomatic(hints);CHECK(fixture::deep==1); // terminal request does not repeat
 }
 reset();fixture::supply=true;Power::pollAutomatic(0);fixture::now=900000;Power::pollAutomatic(0);CHECK(fixture::lit&&!fixture::deep);
 fixture::lit=false;Power::pollAutomatic(0);fixture::now+=240000;Power::pollAutomatic(0);CHECK(!fixture::deep&&!fixture::economy&&!fixture::lit);
 fixture::config.sleepOnPower=1;Power::pollAutomatic(0);CHECK(!fixture::deep);
 fixture::now+=240000;Power::pollAutomatic(0);CHECK(fixture::deep==1);
 reset();fixture::lit=false;Power::pollAutomatic(0);fixture::now=239000;Power::pollAutomatic(0);
 fixture::lit=true;Power::activity();Power::pollAutomatic(0);fixture::lit=false;Power::pollAutomatic(0);
 fixture::now+=239999;Power::pollAutomatic(0);CHECK(!fixture::deep);++fixture::now;Power::pollAutomatic(0);CHECK(fixture::deep==1);
 reset();fixture::config.automatic=0;fixture::lit=false;Power::pollAutomatic(0);fixture::now=999999;Power::pollAutomatic(0);CHECK(!fixture::economy&&!fixture::deep);
 for(unsigned mask=0;mask<32;++mask){
  reset();fixture::lit=false;fixture::supply=mask&1;fixture::config.sleepOnPower=bool(mask&2);
  fixture::connected=mask&4;fixture::config.protectLinks=bool(mask&8);fixture::config.automatic=bool(mask&16);
  const bool allowed=fixture::config.automatic&&(!fixture::supply||fixture::config.sleepOnPower);
  const bool deepAllowed=allowed&&!(fixture::connected&&fixture::config.protectLinks);
  Power::pollAutomatic(0);fixture::now=240000;Power::pollAutomatic(0);
  CHECK(fixture::economy==allowed);CHECK(bool(fixture::deep)==deepAllowed);CHECK(Power::automaticDeepSleepAllowed()==deepAllowed);
 }
 // Inserting power restores only automatically darkened displays.
 reset();Power::pollAutomatic(0);fixture::now=60000;Power::pollAutomatic(0);CHECK(!fixture::lit);
 fixture::supply=true;Power::pollAutomatic(0);CHECK(fixture::lit);
 fixture::lit=false;Power::pollAutomatic(0);CHECK(!fixture::lit);
 fixture::supply=false;Power::pollAutomatic(0);fixture::now+=239999;Power::pollAutomatic(0);CHECK(!fixture::deep);
 ++fixture::now;Power::pollAutomatic(0);CHECK(fixture::deep==1);
 // Link protection allows standby, then starts a full deep wait on disconnect.
 reset();fixture::lit=false;fixture::connected=true;fixture::config.protectLinks=1;
 Power::pollAutomatic(0);fixture::now=900000;Power::pollAutomatic(0);CHECK(fixture::economy&&!fixture::deep);
 fixture::connected=false;Power::pollAutomatic(0);CHECK(!fixture::deep);
 fixture::now+=239999;Power::pollAutomatic(0);CHECK(!fixture::deep);
 ++fixture::now;Power::pollAutomatic(0);CHECK(fixture::deep==1);
 std::printf("%u production power-runtime checks: blackout deadlines, all busy sessions, supply, links, settings, wake reset, USB admission and one-shot deep request\n",checks);
}
