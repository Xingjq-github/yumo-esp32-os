#include "host_services.hpp"
#include "iot_button.h"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

HostButton buttons[6];
static unsigned checks=0,shots=0,toggles=0,shutdowns=0;
static int volume=50;
static int64_t now=1000000;
static bool screen=true,sleeping=false,queueFull=false;
static std::vector<sys::Event> events;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int64_t esp_timer_get_time(){return now;}
int sys::Settings::volume(){return ::volume;}
void sys::SystemService::setVolume(int value){volume=value;}
void sys::Screenshot::request(){++shots;}
void sys::EventBus::publish(const Event &event){if(!queueFull)events.push_back(event);}
bool sys::Power::isScreenOn(){return screen;}
bool sys::Power::sleepPending(){return sleeping;}
bool sys::Power::wokeFromSleep(){return false;}
bool sys::Power::slowIdle(){return false;}
void sys::Power::activity(){}
void sys::Power::screenOn(){screen=true;}
void sys::Power::toggleScreen(){++toggles;screen=!screen;}
void sys::Power::powerOff(){++shutdowns;}
static void emit(int pin,button_event_t event){now+=10000;auto &b=buttons[pin];b.event=event;CHECK(b.cb[event]);b.cb[event](&b,nullptr);}
static void down(int pin){emit(pin,BUTTON_PRESS_DOWN);emit(pin,BUTTON_PRESS_UP);}
static void finish(int pin){emit(pin,BUTTON_SINGLE_CLICK);emit(pin,BUTTON_PRESS_END);}
static void tap(int pin){down(pin);finish(pin);}
static void doubleTap(int pin){down(pin);down(pin);emit(pin,BUTTON_DOUBLE_CLICK);emit(pin,BUTTON_PRESS_END);}
static void clear(){events.clear();volume=50;shots=0;queueFull=false;}

int main(){using namespace sys;
 Buttons::init();
 for(int cycle=0;cycle<100;++cycle)for(int pin:{0,4}){
  const auto type=pin==0?EventType::PreviousPage:EventType::NextPage;
  const int delta=pin==0?-5:5;
  clear();auto a=Buttons::claimPageKeys();tap(pin);
  CHECK(events.size()==1 && events[0].type==type && events[0].i32==int32_t(a) && volume==50);
  // Gesture began in A; B is opened while iot_button resolves single vs double.
  clear();down(pin);Buttons::releasePageKeys(a);auto b=Buttons::claimPageKeys();finish(pin);
#ifdef EXPECT_OLD
  CHECK(events.size()==1 && events[0].i32==int32_t(b));
#else
  CHECK(events.empty() && volume==50);
#endif
  // A delayed click after returning home stays consumed, never changes volume.
  clear();down(pin);Buttons::releasePageKeys(b);finish(pin);
#ifdef EXPECT_OLD
  CHECK(events.empty() && volume==50+delta);
#else
  CHECK(events.empty() && volume==50);
#endif
  // A home press must not become a page action in a newly opened reader.
  clear();down(pin);a=Buttons::claimPageKeys();finish(pin);
#ifdef EXPECT_OLD
  CHECK(events.size()==1 && events[0].i32==int32_t(a));
#else
  CHECK(events.empty() && volume==50);
#endif
  clear();tap(pin);CHECK(events.size()==1 && events[0].i32==int32_t(a));
  // Already queued events retain the old token when the consumer changes.
  auto queued=events[0];Buttons::releasePageKeys(a);b=Buttons::claimPageKeys();CHECK(queued.i32!=int32_t(b));
  clear();queueFull=true;tap(pin);CHECK(events.empty() && volume==50);queueFull=false;
  // Two physical keys have independent captured sessions.
  clear();down(pin);Buttons::releasePageKeys(b);a=Buttons::claimPageKeys();down(pin==0?4:0);
  finish(pin);finish(pin==0?4:0);
#ifdef EXPECT_OLD
  CHECK(events.size()==2);
#else
  CHECK(events.size()==1 && events[0].i32==int32_t(a) && events[0].type!=type);
#endif
  Buttons::releasePageKeys(a);clear();tap(pin);CHECK(events.empty() && volume==50+delta);
  // A press begun while dark/preparing sleep must not turn into a page action on wake.
  a=Buttons::claimPageKeys();
  for(bool standby:{false,true}) {
   clear();screen=standby;sleeping=standby;down(pin);screen=true;sleeping=false;finish(pin);
#ifdef EXPECT_OLD
   CHECK(events.size()==1 && events[0].i32==int32_t(a));
#else
   CHECK(events.empty() && volume==50);
#endif
  }
  // A lit-page press completed after screen-off also remains consumed.
  clear();down(pin);screen=false;finish(pin);screen=true;
#ifdef EXPECT_OLD
  CHECK(events.size()==1 && events[0].i32==int32_t(a));
#else
  CHECK(events.empty() && volume==50);
#endif
  Buttons::releasePageKeys(a);
  // Panel ownership continues to win and captures delayed clicks on dismissal.
  a=Buttons::claimPageKeys();clear();Buttons::setSystemPanel(true);tap(pin);CHECK(events.empty() && volume==50+delta);
  clear();down(pin);Buttons::setSystemPanel(false);finish(pin);CHECK(events.empty() && volume==50);
  Buttons::releasePageKeys(a);
  // Existing global double-click rules are unchanged by page ownership.
  clear();a=Buttons::claimPageKeys();doubleTap(pin);
  CHECK(volume==50);if(pin==0)CHECK(events.size()==1 && events[0].type==EventType::HomeRequested);else CHECK(shots==1 && events.empty());
  Buttons::releasePageKeys(a);
  // Foreground game owns singles and its BOOT double; KEY double stays screenshot.
  clear();auto game=Buttons::claimGameKeys();Buttons::gameMode(game,true);Buttons::gameRunning(game,true);
  tap(pin);CHECK((Buttons::takeGameKeys(game)&(pin==0?2:1))!=0 && events.empty() && volume==50);
  doubleTap(0);CHECK(events.empty());Buttons::releaseGameKeys(game);
  // Stopwatch consumes KEY/PWR at release and maps BOOT double to Home mailbox.
  clear();auto timer=Buttons::claimTimerKeys();tap(4);TimerKey key;
  CHECK(Buttons::takeTimerKey(timer,key) && key.action==TimerKey::Key && shots==0 && volume==50);
  tap(5);CHECK(Buttons::takeTimerKey(timer,key) && key.action==TimerKey::Power);
  doubleTap(0);CHECK(Buttons::takeTimerKey(timer,key) && key.action==TimerKey::Home && events.empty());
  doubleTap(4);CHECK(shots==0 && volume==50);Buttons::releaseTimerKeys(timer);
  // BOOT back remains an independent single-click owner.
  clear();auto back=Buttons::claimBootBack();tap(0);CHECK(Buttons::takeBootBack(back) && events.empty() && volume==50);Buttons::releaseBootBack(back);
 }
 CHECK(toggles==0 && shutdowns==0);
#ifdef EXPECT_OLD
 std::printf("%u checks; 200 stale-page/home/new-page reproductions; priority/double-click regressions preserved\n",checks);
#else
 std::printf("%u checks; 200 per-key lifecycle suites; stale events, queue full, panel, game, timer, back and doubles\n",checks);
#endif
}
