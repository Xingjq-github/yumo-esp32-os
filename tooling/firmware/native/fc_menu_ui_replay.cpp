// Real FC menu widgets and option callbacks; hardware and emulator are doubles.
#include "lvgl.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "core/button_mgr.hpp"
#include "core/keyboard_input.hpp"
#include "core/ble_keyboard_client.hpp"
#include "ui/external_keyboard.hpp"
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/control_states.h"
#define private public
#include "apps/fc_player.hpp"
#undef private
static unsigned checks=0,releases=0,clears=0,notifications=0;
static bool lit=true,sleeping=false;
static bool keyboardLinked=false,gamepadLinked=false;
static bool iineGamepadProfile=false;
static sys::hidinput::Keys keyboardState;
static sys::BleBond peer;
static sys::BleBond requestedBond;
static sys::BluetoothSnapshot bluetoothState;
static sys::BleKeyboardClient::Diagnostics inputDiagnostics;
static bool serviceBusy=false,diagnosticVisible=false;
static sys::ServiceResult serviceResult;
static unsigned scanCancels=0,connectionCancels=0;
static unsigned serviceRequests=0;
static ui::ExternalKeyboard::RawHandler capturedKeys=nullptr;
static void *capturedContext=nullptr;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace ui {void TextInput::hide(){}void TextInput::show(lv_obj_t *){}bool TextInput::visible(){return false;}void TextInput::bindField(lv_obj_t *){}}
namespace core {void WorkerWake::notify(){++notifications;}}
namespace core::fc {Job::~Job()=default;}
namespace sys {
BluetoothSnapshot SystemService::bluetooth(){return bluetoothState;}
bool SystemService::busy(ServiceDomain){return serviceBusy;}
ServiceResult SystemService::result(ServiceDomain){return serviceResult;}
esp_err_t SystemService::request(ServiceAction,bool){++serviceRequests;return ESP_OK;}
esp_err_t SystemService::connectKeyboard(const BleScanDevice &){return ESP_OK;}
esp_err_t SystemService::reconnectBond(const BleBond &value){requestedBond=value;return ESP_OK;}
void SystemService::cancelBluetoothScan(){++scanCancels;}
void SystemService::cancelBluetoothConnection(){++connectionCancels;}
const char *SystemService::errorText(esp_err_t){return "错误";}
int SystemService::nearby(BleScanDevice *,int){return 0;}
BleKeyboardClient::Diagnostics BleKeyboardClient::diagnostics(){return inputDiagnostics;}
void BleKeyboardClient::diagnosticView(bool visible){diagnosticVisible=visible;}
bool BleKeyboardClient::iineKeyboard(){return keyboardLinked&&gamepad::iineKeyboard(::peer.name);}
bool BleKeyboardClient::iineGamepad(){return gamepadLinked&&iineGamepadProfile;}
void BleHid::formatAddr(const BleBond &,char *out,size_t length){std::snprintf(out,length,"00:00:00:00:00:01");}
void Buttons::gameRunning(uint32_t,bool){}
void Buttons::clearGameKeys(uint32_t){++clears;}
hidinput::Keys KeyboardInput::snapshot(uint32_t &revision){revision=1;return keyboardState;}
bool BleHid::keyboardConnected(){return keyboardLinked;}
bool BleHid::gamepadConnected(){return gamepadLinked;}
BleBond BleHid::peer(){return ::peer;}
class Power {public:static bool isScreenOn(){return lit;}static bool sleepPending(){return sleeping;}};
}
namespace ui {void ExternalKeyboard::capture(RawHandler handler,void *context){capturedKeys=handler;capturedContext=context;}}
namespace apps {
void FcPlayer::onCreate(lv_obj_t *){}void FcPlayer::onHide(){}void FcPlayer::onDestroy(){}
bool FcPlayer::onBack(){return false;}
void FcPlayer::releaseTilt(){++releases;tiltSession=0;tilt.clear();tiltReady=false;}
void FcPlayer::showSaves(){}void FcPlayer::closeSaves(){}void FcPlayer::stopGame(bool){job->stop=true;}
void FcPlayer::showList(){closeBluetooth();page=Page::List;lv_obj_clean(root());}
namespace c=ui::compact;
namespace padui=core::fc::controls;
namespace tiltui=core::fc::tilt;
}
#include "compact_ui.inc"
#include "fc_menu.inc"
#include "fc_pause.inc"
#include "fc_input.inc"
#include "fc_controls.inc"
#include "fc_bluetooth.inc"
static uint8_t framebuffer[240*240*2];
static void advance(){lv_tick_inc(300);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void click(lv_obj_t *o){CHECK(o);CHECK(!lv_obj_has_state(o,LV_STATE_DISABLED));lv_obj_send_event(o,LV_EVENT_CLICKED,nullptr);advance();}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
int main(){
 lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 // Actual pointer events: hiding the decoration must also disable its hit area,
 // while Select/Start/Menu remain usable and disconnect restores the joystick.
 {
  static lv_point_t position{};static bool pressed=false;
  auto pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer,[](lv_indev_t *,lv_indev_data_t *d){d->point=position;d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
  ui::theme::init(4);auto r=ui::compact::panel(lv_screen_active(),240,240);apps::FcPlayer a;a.setRoot(r);
  a.page=apps::FcPlayer::Page::Game;a.job=std::make_shared<core::fc::Job>();a.job->ready=true;a.job->paused=false;a.wasPaused=false;
  gamepadLinked=true;a.createControls();a.createMenu();lv_obj_set_hidden(a.overlay,true);advance();a.pollGamepad();
  auto touch=[&](int x,int y,bool down){position={lv_coord_t(x),lv_coord_t(y)};pressed=down;lv_tick_inc(25);lv_indev_read(pointer);};
  touch(80,33,true);CHECK(a.job->buttons==0&&a.touch.pad==0);touch(80,33,false);
  touch(40,200,true);CHECK(a.job->buttons==4);touch(40,200,false);CHECK(a.job->buttons==0);
  touch(200,200,true);CHECK(a.job->buttons==8);touch(200,200,false);
  gamepadLinked=false;a.pollGamepad();touch(80,33,true);CHECK(a.job->buttons==0x80);touch(80,33,false);CHECK(a.job->buttons==0);
  touch(120,200,true);CHECK(a.job->paused&&a.job->buttons==0);touch(120,200,false);
  lv_indev_delete(pointer);a.job.reset();lv_obj_delete(r);advance();
 }
 for(int theme:{4,5,6,8,10}){size_t baseline=0;for(int cycle=0;cycle<8;++cycle){
  ui::theme::init(theme);auto r=ui::compact::panel(lv_screen_active(),240,240);apps::FcPlayer a;a.setRoot(r);
  a.page=apps::FcPlayer::Page::Game;a.job=std::make_shared<core::fc::Job>();a.createMenu();advance();
  CHECK(lv_obj_has_state(a.tiltToggle,LV_STATE_DISABLED));CHECK(lv_obj_has_state(a.joystickToggle,LV_STATE_DISABLED));
  CHECK(!std::strcmp(lv_label_get_text(lv_obj_get_child(a.joystickToggle,0)),"摇杆:8F"));
  a.job->ready=true;a.job->paused=true;a.refreshControlOptions();advance();
  CHECK(lv_obj_get_child_count(a.overlay)==7);
  for(unsigned i=1;i<lv_obj_get_child_count(a.overlay);++i){
   auto b=lv_obj_get_child(a.overlay,i);lv_area_t bounds{};lv_obj_get_coords(b,&bounds);
   CHECK(bounds.x1>=0&&bounds.y1>=58&&bounds.x2<240&&bounds.y2<240);CHECK(lv_obj_get_height(b)==40);
   for(unsigned j=i+1;j<lv_obj_get_child_count(a.overlay);++j){lv_area_t other{};lv_obj_get_coords(lv_obj_get_child(a.overlay,j),&other);
    CHECK(bounds.x2<other.x1 || other.x2<bounds.x1 || bounds.y2<other.y1 || other.y2<bounds.y1);}
  }
  const auto released=releases,cleared=clears;click(a.tiltToggle);
  CHECK(a.tiltEnabled&&a.job->paused&&releases==released+1&&clears==cleared+1&&!a.tiltSession);
  CHECK(lv_obj_has_state(a.tiltToggle,LV_STATE_CHECKED));
  a.touch.pad=0xf0;a.job->buttons=0xf0;a.job->pressed=0xf0;click(a.joystickToggle);
  CHECK(a.touch.fourWay&&!a.touch.pad&&!a.job->buttons&&!a.job->pressed&&a.job->paused);
  CHECK(!std::strcmp(lv_label_get_text(lv_obj_get_child(a.joystickToggle,0)),"摇杆:4F"));
  click(a.joystickToggle);CHECK(!a.touch.fourWay);click(a.tiltToggle);CHECK(!a.tiltEnabled);
  CHECK(!lv_obj_has_state(a.tiltToggle,LV_STATE_CHECKED));
  a.saveBusy=true;a.refreshControlOptions();CHECK(lv_obj_has_state(a.tiltToggle,LV_STATE_DISABLED));
  a.toggleTilt();a.toggleJoystick();CHECK(!a.tiltEnabled&&!a.touch.fourWay);a.saveBusy=false;
  a.job->paused=false;a.refreshControlOptions();CHECK(lv_obj_has_state(a.joystickToggle,LV_STATE_DISABLED));
  a.toggleTilt();a.toggleJoystick();CHECK(!a.tiltEnabled&&!a.touch.fourWay);
  // Real showMenu/resumeGame: a dark poll only pauses/clears once.
  a.wasPaused=false;lit=false;
  const auto c0=clears,r0=clears,n0=notifications;
  for(unsigned i=0;i<1000;++i)a.showMenu();
  CHECK(clears==c0+1 && notifications==n0+1 && a.job->paused);
  a.resumeGame();CHECK(a.job->paused);lit=true;a.resumeGame();
  CHECK(!a.job->paused && !a.wasPaused && clears==r0+2);
  a.job->paused=true;a.job->stop=true;a.refreshControlOptions();a.toggleTilt();a.toggleJoystick();CHECK(!a.tiltEnabled&&!a.touch.fourWay);
  a.job.reset();lv_obj_delete(r);advance();if(!cycle)baseline=memory();else CHECK(memory()==baseline);
 }}
 // Actual FC input functions: GATT state is synthetic, mapping/lifecycle is real.
 for(unsigned cycle=0;cycle<30;++cycle){
  auto r=ui::compact::panel(lv_screen_active(),240,240);apps::FcPlayer a;a.setRoot(r);
  a.page=apps::FcPlayer::Page::Game;a.job=std::make_shared<core::fc::Job>();a.createMenu();a.job->ready=true;a.job->paused=false;a.wasPaused=false;
  auto consume=[&](){return uint8_t(a.job->buttons.load()|a.job->pressed.exchange(0));};
  sys::gamepad::reset();keyboardLinked=false;gamepadLinked=false;a.createControls();advance();
  CHECK(!lv_obj_is_hidden(a.stick)&&!lv_obj_is_hidden(a.thumb));
  a.touch.begin(60,33);a.sendInput();CHECK(consume()==0x80);
  gamepadLinked=true;a.pollGamepad();CHECK(consume()==0);
  CHECK(lv_obj_is_hidden(a.stick)&&lv_obj_is_hidden(a.thumb)&&a.touch.pad==0);
  sys::gamepad::publish({1,0x40});a.pollGamepad();CHECK(consume()==0x41);
  // Two turns before the emulator consumes a frame must never press L+R.
  sys::gamepad::publish({1,0x40});a.pollGamepad();sys::gamepad::publish({1,0x80});a.pollGamepad();CHECK(consume()==0x81);
  sys::gamepad::publish({});a.pollGamepad();CHECK(consume()==0);
  sys::gamepad::publish({2,0x10});sys::gamepad::publish({});a.pollGamepad();CHECK(consume()==0x12);CHECK(consume()==0);
  // Current right wins over a quickly released left, with button tap retained.
  sys::gamepad::publish({2,0x40});sys::gamepad::publish({0,0x80});a.pollGamepad();CHECK(consume()==0x82);
  a.showMenu();CHECK(a.job->paused&&consume()==0);a.resumeGame();a.pollGamepad();CHECK(consume()==0);
  sys::gamepad::publish({});a.pollGamepad();sys::gamepad::publish({3,0x90});a.pollGamepad();CHECK(consume()==0x93);
  // Real C-mode layout has B=button 1, A=button 2. Preserve held directions
  // and quick taps, with exactly one swap for both current and queued input.
  sys::gamepad::publish({});a.pollGamepad();CHECK(consume()==0);iineGamepadProfile=true;
  sys::gamepad::publish({1,0x80});a.pollGamepad();CHECK(consume()==0x82);
  sys::gamepad::publish({});a.pollGamepad();CHECK(consume()==0);
  sys::gamepad::publish({2,0});sys::gamepad::publish({});a.pollGamepad();CHECK(consume()==1);CHECK(consume()==0);
  iineGamepadProfile=false;
  gamepadLinked=false;sys::gamepad::reset();a.pollGamepad();CHECK(consume()==0);
  CHECK(!lv_obj_is_hidden(a.stick)&&!lv_obj_is_hidden(a.thumb));
  // Reconnect with held controls: ignore until neutral and pressed again.
  gamepadLinked=true;sys::gamepad::publish({1,0x10});a.pollGamepad();CHECK(consume()==0);
  sys::gamepad::publish({});a.pollGamepad();sys::gamepad::publish({1,0x10});a.pollGamepad();CHECK(consume()==0x11);
  lit=false;a.pollGamepad();CHECK(consume()==0);lit=true;a.pollGamepad();CHECK(consume()==0);
  sys::gamepad::publish({});a.pollGamepad();sleeping=true;sys::gamepad::publish({1,0});a.pollGamepad();CHECK(consume()==0);sleeping=false;
  gamepadLinked=false;sys::gamepad::reset();a.pollGamepad();keyboardLinked=true;keyboardState={};a.clearInput();
  std::strcpy(peer.name,"IINE-keyboard");
  apps::FcPlayer::keys({},&a);
  // H mode is reserved for system navigation. Even a direct raw callback
  // cannot generate pad presses, Pause/Start/Exit, or replay released taps.
  for(uint8_t usage:{uint8_t(0x2c),uint8_t(0x28)}){
   keyboardState.set(usage);apps::FcPlayer::keys(keyboardState,&a);
   keyboardState={};apps::FcPlayer::keys(keyboardState,&a);
   CHECK(consume()==0&&!a.job->paused&&!a.job->stop);CHECK(consume()==0);
  }
  a.job->mapping.usage[core::fc::Exit]=0x28;a.job->mapping.usage[core::fc::Pause]=0x2c;
  keyboardState.set(0x28);keyboardState.set(0x2c);apps::FcPlayer::keys(keyboardState,&a);
  CHECK(consume()==0&&!a.job->paused&&!a.job->stop);
  a.clearInput();apps::FcPlayer::keys(keyboardState,&a);CHECK(consume()==0);
  keyboardState={};apps::FcPlayer::keys(keyboardState,&a);
  // Deliberately bind A/B/arrows to other actions. H remains outside the game.
  a.job->mapping.usage[core::fc::Exit]=0x04;a.job->mapping.usage[core::fc::Pause]=0x05;
  a.job->mapping.usage[core::fc::Select]=0x52;
  apps::FcPlayer::keys({},&a);
  keyboardState.set(0x04);keyboardState.set(0x05);keyboardState.set(0x52);
  apps::FcPlayer::keys(keyboardState,&a);CHECK(consume()==0&&!a.job->paused&&!a.job->stop);
  a.clearInput();apps::FcPlayer::keys(keyboardState,&a);CHECK(consume()==0);
  keyboardState={};apps::FcPlayer::keys(keyboardState,&a);keyboardState.set(0x04);apps::FcPlayer::keys(keyboardState,&a);CHECK(consume()==0);
  keyboardState={};apps::FcPlayer::keys(keyboardState,&a);std::strcpy(peer.name,"Normal keyboard");
  keyboardState.set(0x04);apps::FcPlayer::keys(keyboardState,&a);CHECK(a.job->stop);
  keyboardState={};keyboardLinked=false;a.job.reset();lv_obj_delete(r);advance();
 }
 // Real Bluetooth page: connection stages, input feedback and safe row lifecycle.
 for(int theme:{4,5,6,8,10}){size_t baseline=0;uint32_t baselineObjects=0;for(unsigned cycle=0;cycle<12;++cycle){
  ui::theme::init(theme);auto r=ui::compact::panel(lv_screen_active(),240,240);apps::FcPlayer a;a.setRoot(r);
  bluetoothState={};inputDiagnostics={};serviceBusy=false;serviceResult={};keyboardState={};sys::gamepad::reset();
  bluetoothState.enabled=true;bluetoothState.state=sys::BleHidState::Ready;
  a.showBluetooth();advance();CHECK(diagnosticVisible&&a.btList&&!a.btInput[0]);
  CHECK(std::strstr(lv_label_get_text(a.status),"C档"));
  bluetoothState.state=sys::BleHidState::Connecting;
  for(auto stage:{sys::BleKeyboardClient::Stage::Service,sys::BleKeyboardClient::Stage::Characteristics,sys::BleKeyboardClient::Stage::ReportMap,sys::BleKeyboardClient::Stage::References,sys::BleKeyboardClient::Stage::Subscribe}){
   inputDiagnostics.stage=stage;a.pollBluetooth();CHECK(std::strstr(lv_label_get_text(a.status),"已配对"));
  }
  bluetoothState.state=sys::BleHidState::Connected;bluetoothState.peer.connected=bluetoothState.peer.keyboard=true;
  inputDiagnostics.stage=sys::BleKeyboardClient::Stage::Ready;inputDiagnostics.received=12;
  a.btPressed=true;a.pollBluetooth();CHECK(!a.btInput[0]);a.btPressed=false;a.pollBluetooth();advance();
  CHECK(a.btInput[0]&&std::strstr(lv_label_get_text(a.btPackets),"12"));
  CHECK(capturedKeys==apps::FcPlayer::bluetoothKeys&&capturedContext==&a);
  const auto requests=serviceRequests;
  for(uint8_t key:{uint8_t(0x28),uint8_t(0x29),uint8_t(0x2c)}){
   sys::hidinput::Keys report;report.set(key);capturedKeys(report,capturedContext);capturedKeys({},capturedContext);
   a.pollBluetooth();CHECK(a.btLastKey==key&&a.page==apps::FcPlayer::Page::Bluetooth&&serviceRequests==requests);
  }
  CHECK(std::strstr(lv_label_get_text(a.btCode),"2C"));
  keyboardLinked=true;std::strcpy(peer.name,"IINE-keyboard");
  for(uint8_t key:{uint8_t(0x2c),uint8_t(0x28)}){
   advance();sys::hidinput::Keys report;report.set(key);capturedKeys(report,capturedContext);capturedKeys({},capturedContext);
   a.pollBluetooth();CHECK(lv_obj_has_state(a.btInput[key==0x28?4:5],LV_STATE_CHECKED));
   CHECK(!lv_obj_has_state(a.btInput[key==0x28?5:4],LV_STATE_CHECKED));
   CHECK(a.page==apps::FcPlayer::Page::Bluetooth&&serviceRequests==requests);
  }
  advance();a.pollBluetooth();CHECK(!lv_obj_has_state(a.btInput[4],LV_STATE_CHECKED)&&!lv_obj_has_state(a.btInput[5],LV_STATE_CHECKED));
  std::strcpy(peer.name,"Normal keyboard");sys::hidinput::Keys normal;normal.set(0x28);normal.set(0x2c);
  CHECK(apps::FcPlayer::bluetoothKeyPad(normal)==0);keyboardLinked=false;
  sys::hidinput::Keys tap;tap.set(0x52);capturedKeys(tap,capturedContext);capturedKeys({},capturedContext);
  a.pollBluetooth();CHECK(lv_obj_has_state(a.btInput[0],LV_STATE_CHECKED));advance();a.pollBluetooth();CHECK(!lv_obj_has_state(a.btInput[0],LV_STATE_CHECKED));
  lv_area_t codeBounds{};lv_obj_get_coords(a.btCode,&codeBounds);CHECK(codeBounds.y2<240);
  const uint8_t usages[]={0x52,0x51,0x50,0x4f,4,5};
  for(unsigned pressed=0;pressed<6;++pressed){
   keyboardState={};keyboardState.set(usages[pressed]);a.pollBluetooth();advance();
   for(unsigned i=0;i<6;++i){CHECK(lv_obj_has_state(a.btInput[i],LV_STATE_CHECKED)==(i==pressed));
    lv_area_t b{};lv_obj_get_coords(a.btInput[i],&b);CHECK(b.x1>=0&&b.x2<240&&b.y1>=154&&b.y2<240);}
  }
  keyboardState={};gamepadLinked=true;sys::gamepad::publish({3,0x90});a.pollBluetooth();
  CHECK(lv_obj_has_state(a.btInput[0],LV_STATE_CHECKED)&&lv_obj_has_state(a.btInput[3],LV_STATE_CHECKED));
  CHECK(lv_obj_has_state(a.btInput[4],LV_STATE_CHECKED)&&lv_obj_has_state(a.btInput[5],LV_STATE_CHECKED));
  sys::gamepad::publish({});advance();a.pollBluetooth();iineGamepadProfile=true;
  sys::gamepad::publish({1,0});sys::gamepad::publish({});a.pollBluetooth();
  CHECK(lv_obj_has_state(a.btInput[5],LV_STATE_CHECKED)&&!lv_obj_has_state(a.btInput[4],LV_STATE_CHECKED));
  advance();a.pollBluetooth();CHECK(!lv_obj_has_state(a.btInput[5],LV_STATE_CHECKED));iineGamepadProfile=false;
  inputDiagnostics.rejected=2;a.pollBluetooth();CHECK(std::strstr(lv_label_get_text(a.btPackets),"2 条格式"));
  bluetoothState.peer.connected=false;bluetoothState.state=sys::BleHidState::Ready;gamepadLinked=false;
  a.pollBluetooth();advance();CHECK(!a.btInput[0]&&!a.btPackets&&lv_obj_get_child_count(a.btList)==1);
  bluetoothState.bondCount=8;++bluetoothState.bondRevision;
  for(unsigned i=0;i<8;++i){auto &p=bluetoothState.bonds[i];p.addr[0]=uint8_t(i+1);p.keyboard=true;
   std::snprintf(p.name,sizeof(p.name),"%s",i==0?"IINE-keyboard":i==1?"Xbox Wireless Controller":i==2?"Wireless Controller":"Keyboard");}
  a.pollBluetooth();advance();CHECK(a.btBonds.size()==8&&lv_obj_get_child_count(a.btList)==8);
  auto last=lv_obj_get_child(a.btList,7);lv_obj_scroll_to_view_recursive(last,LV_ANIM_OFF);advance();
  lv_area_t bounds{};lv_obj_get_coords(last,&bounds);CHECK(bounds.y1>=154&&bounds.y2<240);
  for(unsigned i:{0u,1u,2u,7u}){
   click(lv_obj_get_child(a.btList,i));CHECK(requestedBond.addr[0]==i+1);
   CHECK(!std::strcmp(requestedBond.name,bluetoothState.bonds[i].name));
   serviceResult.action=sys::ServiceAction::BluetoothReconnect;++serviceResult.revision;a.pollBluetooth();
  }
  const auto cancelCount=connectionCancels;a.btOwnConnection=true;a.closeBluetooth();
  CHECK(!diagnosticVisible&&connectionCancels==cancelCount+1&&!a.btList&&!a.btInput[0]&&!capturedKeys);
  lv_obj_delete(r);advance();lv_mem_monitor_t heap{};lv_mem_monitor(&heap);
  if(!cycle){baseline=heap.free_size;baselineObjects=heap.used_cnt;}else {
   // Native LVGL render allocations oscillate by 24 bytes in this page replay.
   // Require no retained allocation growth and bounded bytes over 12 cycles.
   CHECK(heap.used_cnt==baselineObjects&&heap.free_size+32>=baseline&&heap.free_size<=baseline+32);
  }
  CHECK(lv_anim_count_running()==0);
 }}
 std::printf("{\"passed\":true,\"checks\":%u,\"theme_cycles\":40,\"fc_input_cycles\":30,\"bluetooth_page_cycles\":60,\"heap_object_count_restored\":true,\"bluetooth_heap_slack_bytes\":32}\n",checks);lv_deinit();
}
