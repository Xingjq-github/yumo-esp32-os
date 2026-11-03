#include "core/power_mgr.hpp"
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include "core/power_policy.hpp"
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGI(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)
namespace sys {
static std::atomic<bool> shuttingDown{false},sleepRequested{false},uiStopped{false},automaticSleepRequested{false},wakeRequested{false},standbyActive{false},terminalPhase{false},sleepReady{false},themeRestartReady{false},themeRestartFailed{false},shutdownFailed{false};
static std::atomic<int64_t> sleepRequestedAt{0};
static bool shutdownRequested=false,displayNeedsResume=false,lit=true;
static int restartTheme=0,sleepTask=1,kPwrPin=5;
static int64_t now=100;
static std::mutex displayMutex;
static bool usbIdle=true,castIdle=true,shotIdle=true,keysIdle=true,mouseIdle=true,lanIdle=true,storageBusy=false,workersIdle=true,flushOk=true;
static int cut=0,flushes=0,unmounts=0,notifications=0;
static bool usbStop=false;
static power::Config config;
static bool supply=false,connected=false;
static int lateProtection=0;
static bool linkConnected(){return connected;}
static void protect(int kind){
    if(kind==1)supply=true;
    if(kind==2){config.protectLinks=1;connected=true;}
    if(kind==3)config.automatic=0;
    if(kind==4)config.deepSeconds=0;
}
static int64_t esp_timer_get_time(){return now;}
static int gpio_get_level(int){return 1;}
static void setScreenLocked(bool on){lit=on;}
static void holdPerformance(bool){}
static void requestRecovery(const char *){wakeRequested=true;}
static int bsp_display_resume(){return 0;}
static int bsp_display_suspend(){return 0;}
static void xTaskNotifyGive(int){++notifications;}
static int bsp_sdcard_get_handle(){return 0;}
static int bsp_sdcard_unmount(){return 0;}
static int esp_wifi_stop(){return 0;}
struct UsbStorage{static bool idle(){return usbIdle;}static int setEnabled(bool on){CHECK(!on);usbStop=true;return 0;}};
struct SystemService{static bool running(){return true;}static void cancelBluetoothScan(){}static void cancelWifiScan(){}};
struct Battery{static void poll(bool){}static bool supplyProtected(){return supply;}};
struct DisplayCast{static bool idle(){return castIdle;}};
struct Screenshot{static bool idle(){return shotIdle;}};
struct LanHotspot{static bool idle(){return lanIdle;}};
struct BleHid{static bool keysIdle(){return sys::keysIdle;}static bool mouseIdle(){return sys::mouseIdle;}static bool connected(){return false;}static int prepareDeepSleep(){return 0;}};
struct Qmi8658{static bool resumeFromStandby(){return true;}static bool suspendForStandby(){return true;}};
struct Audio{static bool resumeFromStandby(){return true;}static void suspendForStandby(){protect(lateProtection);}};
struct Settings{static power::Config powerConfig(){return config;}static void saveClock(time_t){}static bool flush(bool){++flushes;return flushOk;}static bool saveTheme(themes::Id){return true;}};
namespace core {
namespace xiaozhi {bool idle(){return workersIdle;}}
namespace mp3 {bool idle(){return workersIdle;}}
namespace speed{bool idle(){return workersIdle;}}
namespace sd {enum class RestartState{Ready,Busy,Failed};static RestartState card=RestartState::Ready;bool busy(){return storageBusy;}RestartState prepareRestart(){++unmounts;return card;}}
namespace chip{bool idle(){return workersIdle;}}namespace game_audio{bool idle(){return workersIdle;}}
namespace recorder{bool idle(){return workersIdle;}}namespace fc{bool idle(){return workersIdle;}}
bool storageIdle(){return workersIdle;}
}
namespace vault{bool idle(){return workersIdle;}}
bool Power::isScreenOn(){return lit;}
bool Power::automaticSleepAllowed(){return true;}
void Power::powerOff(){CHECK(flushes>0&&unmounts>0&&uiStopped&&workersIdle&&!storageBusy);++cut;shuttingDown=true;}
#include "shutdown.inc"
static void reset(){config={};supply=connected=false;lateProtection=0;shuttingDown=false;sleepRequested=false;uiStopped=false;wakeRequested=false;standbyActive=false;terminalPhase=false;shutdownRequested=false;automaticSleepRequested=false;restartTheme=0;lit=true;shutdownFailed=false;now=100;cut=flushes=unmounts=notifications=0;usbIdle=castIdle=shotIdle=keysIdle=mouseIdle=lanIdle=workersIdle=flushOk=true;storageBusy=usbStop=false;core::sd::card=core::sd::RestartState::Ready;}
}
int main(){using namespace sys;
 for(int cycle=0;cycle<100;++cycle){
  reset();CHECK(Power::shutdown()==ESP_OK&&sleepRequested&&!lit);Power::processStandby();CHECK(cut==0&&flushes==0);
  uiStopped=true;
  for(auto gate:{&castIdle,&shotIdle,&keysIdle,&mouseIdle,&lanIdle,&workersIdle}){*gate=false;Power::processStandby();CHECK(cut==0);*gate=true;}
  storageBusy=true;Power::processStandby();CHECK(cut==0);storageBusy=false;
  core::sd::card=core::sd::RestartState::Busy;Power::processStandby();CHECK(cut==0);
  core::sd::card=core::sd::RestartState::Ready;Power::processStandby();CHECK(cut==1&&terminalPhase);
  reset();CHECK(Power::shutdown()==ESP_OK);uiStopped=true;flushOk=false;Power::processStandby();CHECK(cut==0&&wakeRequested);Power::processStandby();CHECK(!sleepRequested&&lit&&shutdownFailed);
  reset();Power::shutdown();uiStopped=true;core::sd::card=core::sd::RestartState::Failed;Power::processStandby();CHECK(!cut&&wakeRequested);Power::processStandby();CHECK(!sleepRequested&&lit);
  reset();Power::shutdown();uiStopped=true;workersIdle=false;now=11000101;Power::processStandby();CHECK(!cut&&wakeRequested);Power::processStandby();CHECK(!sleepRequested);
  reset();usbIdle=false;CHECK(Power::shutdown()!=ESP_OK&&!sleepRequested);
  reset();Power::shutdown();CHECK(Power::shutdown()!=ESP_OK);wakeRequested=true;Power::processStandby();CHECK(!cut&&lit&&!sleepRequested);
  // Automatic deep sleep admits busy USB/media, then drains before terminal entry.
  reset();lit=false;usbIdle=workersIdle=false;
  CHECK(Power::deepSleep(true)==ESP_OK&&sleepRequested&&usbStop&&!lit);
  uiStopped=true;Power::processStandby();CHECK(!terminalPhase&&!flushes);
  usbIdle=true;Power::processStandby();CHECK(!terminalPhase&&!flushes);
  workersIdle=true;Power::processStandby();CHECK(terminalPhase&&sleepReady&&flushes==1&&notifications==2);
  reset();usbIdle=false;CHECK(Power::deepSleep(false)!=ESP_OK&&!usbStop&&!sleepRequested);
  reset();lit=false;Power::deepSleep(true);wakeRequested=true;Power::processStandby();CHECK(!sleepRequested&&lit&&!terminalPhase);
  reset();Power::deepSleep(true);CHECK(!sleepRequested); // screen woke before request acquired its lock
  for(int kind=1;kind<=4;++kind){
   reset();lit=false;protect(kind);CHECK(Power::deepSleep(true)!=ESP_OK&&!sleepRequested&&!usbStop);
   reset();lit=false;Power::deepSleep(true);uiStopped=true;protect(kind);
   Power::processStandby();CHECK(!sleepRequested&&!terminalPhase&&!flushes);
   reset();lit=false;Power::deepSleep(true);uiStopped=true;lateProtection=kind;
   Power::processStandby();CHECK(!terminalPhase&&wakeRequested);
   Power::processStandby();CHECK(!sleepRequested&&!terminalPhase);
   reset();protect(kind);CHECK(Power::deepSleep(false)==ESP_OK&&sleepRequested);
   uiStopped=true;Power::processStandby();CHECK(terminalPhase); // explicit manual sleep remains available
  }
 }
 std::printf("%u shutdown checks; 100 cycles, storage barriers, failures, cancellation and timeout\n",checks);
}
