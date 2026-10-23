#include "fc_player.hpp"
#include "core/ble_hid.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/keyboard_input.hpp"
#include "core/ble_keyboard_client.hpp"
#include "core/power_mgr.hpp"
#include "core/qmi8658.hpp"
#include "core/screen_orientation.hpp"
#include "core/settings_store.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/compact_ui.hpp"
#include "ui/external_keyboard.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include <cstring>
#include <cstdio>
#include <new>
#include <cmath>
namespace apps {
namespace c = ui::compact;
namespace padui = core::fc::controls;
namespace tiltui = core::fc::tilt;
void FcPlayer::onCreate(lv_obj_t *) {
  keySession=sys::Buttons::claimGameKeys();
  screenOffSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) {
    if(page==Page::Game) showMenu();
  });
  screenOnSub=sys::EventBus::subscribe(sys::EventType::ScreenOn,[this](const sys::Event &) {
    if(timer)lv_timer_ready(timer);
  });
  lv_obj_add_event_cb(root(),[](lv_event_t *e) {
    auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
    auto indev=lv_indev_active();
    if(!indev || self->page==Page::Game) return;
    const auto dir=lv_indev_get_gesture_dir(indev);
    if(dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT)
      sys::EventBus::publish({sys::EventType::BackRequested});
  },LV_EVENT_GESTURE,this);
  timer = lv_timer_create(
      [](lv_timer_t *t) {
        static_cast<FcPlayer *>(lv_timer_get_user_data(t))->poll();
      },
      100, this);
  showList();
}
void FcPlayer::releaseImage() {
  if (image) {
    lv_image_cache_drop(&descriptor);
    lv_image_set_src(image, nullptr);
  }
  heap_caps_free(pixels);
  pixels = nullptr;
  image = nullptr;
  descriptor = {};
}
void FcPlayer::showList() {
  releaseTilt();
  if(page==Page::Bluetooth) closeBluetooth();
  sys::Buttons::gameMode(keySession,false);
  clearInput();
  page=Page::List;
  leaveApp=false;
  if(listing) listing->cancelled.store(true);
  listing.reset();
  lv_timer_set_period(timer,100);
  ui::ExternalKeyboard::capture(nullptr, nullptr);
  releaseImage();
  lv_obj_clean(root());
  overlay=pauseText=resume=controls=stick=thumb=tiltHint=joystickToggle=nullptr;
  saveEntry=savePanel=saveDialog=saveTitle=saveList=saveBack=saveRefresh=saveWrite=saveRead=nullptr;
  saveRows={};saveDates={};saveTimes={};saveBusy=saveListReady=false;pauseNotice.clear();
  auto back = c::button(
      root(), LV_SYMBOL_LEFT,
      [](lv_event_t *) { ui::WindowManager::instance().goHome(); }, this, 36,
      36);
  lv_obj_set_pos(back, 0, 0);
  auto title = c::label(root(), "FC游戏", 73);
  lv_obj_set_pos(title, 42, 6);
  tiltToggle=c::button(root(),tiltEnabled?"重力 开":"重力 关",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->toggleTilt();
  },this,78,36);
  lv_obj_set_pos(tiltToggle,119,0);
  lv_obj_set_style_bg_color(tiltToggle,ui::theme::accent(),LV_STATE_CHECKED);
  c::checked(tiltToggle,tiltEnabled);
  auto reload = c::button(
      root(), LV_SYMBOL_REFRESH,
      [](lv_event_t *e) {
        static_cast<FcPlayer *>(lv_event_get_user_data(e))->refresh();
      },
      this, 42, 36);
  lv_obj_set_pos(reload, 198, 0);
  status = c::label(root(), "SD卡 /nes", 240);
  lv_obj_set_pos(status, 0, 39);
  list = c::panel(root(), 240, 139, true);
  lv_obj_set_pos(list, 0, 63);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  auto bt = c::button(
      root(), "蓝牙手柄 / 键盘",
      [](lv_event_t *e) {
        static_cast<FcPlayer *>(lv_event_get_user_data(e))->showBluetooth();
      },
      this, 240, 37);
  lv_obj_set_pos(bt, 0, 203);
  refresh();
}
void FcPlayer::bluetoothNotice(const char *text) {
  btMessage=text;
  btNoticeAt=lv_tick_get();
}
uint8_t FcPlayer::bluetoothKeyPad(const sys::hidinput::Keys &keys) {
  const bool iine=sys::BleKeyboardClient::iineKeyboard();
  uint8_t pad=0;
  for(const auto &binding:sys::gamepad::iineBindings) {
    if(!iine && (binding.usage==0x28 || binding.usage==0x2c))continue;
    if(keys.down(binding.usage))pad|=binding.pad;
  }
  return pad;
}
void FcPlayer::bluetoothKeys(const sys::hidinput::Keys &keys,void *context) {
  auto self=static_cast<FcPlayer *>(context);
  if(self->page!=Page::Bluetooth)return;
  // The input-test page consumes keyboard packets, including Enter/Esc/Space.
  // A controller confirmation key must never activate the Bluetooth switch.
  for(unsigned key=4;key<0xe0;++key)
    if(keys.down(key)&&!self->btDiagnosticKeys.down(key))self->btLastKey=uint8_t(key);
  const auto pad=bluetoothKeyPad(keys);
  constexpr uint8_t bits[]={0x10,0x20,0x40,0x80,1,2};
  for(unsigned i=0;i<6;++i)if(pad&bits[i])self->btInputUntil[i]=lv_tick_get()+250;
  self->btDiagnosticKeys=keys;
}
void FcPlayer::closeBluetooth() {
  ui::ExternalKeyboard::capture(nullptr,nullptr);
  sys::BleKeyboardClient::diagnosticView(false);
  sys::SystemService::cancelBluetoothScan();
  if(btOwnConnection) sys::SystemService::cancelBluetoothConnection();
  btOwnConnection=btPending=btPressed=false;
  btMessage.clear();
  std::vector<sys::BleBond>{}.swap(btBonds);
  std::vector<sys::BleScanDevice>{}.swap(btScanned);
  btToggle=btScan=btDisconnect=btPaired=btList=nullptr;
  btInput={};btPackets=btCode=nullptr;btDiagnosticKeys={};btInputUntil={};btLastKey=0;
  btPeer={};
}
void FcPlayer::showBluetooth() {
  if(page!=Page::List || job) return;
  releaseTilt();
  // A directory worker may still be finishing. It owns its job, never widgets.
  if(listing) listing->cancelled.store(true);
  listing.reset();
  std::vector<core::FileEntry>{}.swap(files);
  page=Page::Bluetooth;
  ui::ExternalKeyboard::capture(bluetoothKeys,this);
  sys::BleKeyboardClient::diagnosticView(true);
  btNearby=false;btListRevision=UINT32_MAX;
  btResultRevision=sys::SystemService::result(sys::ServiceDomain::Bluetooth).revision;
  lv_obj_clean(root());list=tiltToggle=nullptr;
  auto back=c::button(root(),LV_SYMBOL_LEFT,[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->showList();
  },this,36,36);
  lv_obj_set_pos(back,0,0);
  auto title=c::label(root(),"手柄 / 键盘",100);
  lv_obj_set_pos(title,41,7);
  btToggle=c::button(root(),"蓝牙 关",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->bluetoothAction(BtAction::Toggle);
  },this,96,36);
  lv_obj_set_pos(btToggle,144,0);
  status=c::label(root(),"",240);lv_obj_set_pos(status,0,37);
  lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(status,44);
  lv_obj_set_style_text_line_space(status,0,0);
  auto peer=c::panel(root(),240,34);lv_obj_set_pos(peer,0,82);
  btPeer.create(peer,238);
  btPaired=c::button(root(),"已配对",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->bluetoothAction(BtAction::Paired);
  },this,70,36);lv_obj_set_pos(btPaired,0,117);
  btScan=c::button(root(),"搜索",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->bluetoothAction(BtAction::Scan);
  },this,88,36);lv_obj_set_pos(btScan,71,117);
  btDisconnect=c::button(root(),"断开",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->bluetoothAction(BtAction::Disconnect);
  },this,80,36);lv_obj_set_pos(btDisconnect,160,117);
  for(auto button:{btToggle,btPaired,btScan})
    lv_obj_set_style_bg_color(button,ui::theme::accent(),LV_STATE_CHECKED);
  btList=c::panel(root(),240,86,true);lv_obj_set_pos(btList,0,154);
  btInput={};btPackets=btCode=nullptr;btDiagnosticKeys={};btInputUntil={};btLastKey=0;
  lv_obj_set_flex_flow(btList,LV_FLEX_FLOW_COLUMN);
  pollBluetooth();
}
void FcPlayer::bluetoothAction(BtAction action,size_t index) {
  if(page!=Page::Bluetooth) return;
  if(action==BtAction::Paired) {
    sys::SystemService::cancelBluetoothScan();
    btNearby=false;btListRevision=UINT32_MAX;pollBluetooth();return;
  }
  if(btPending || sys::SystemService::busy(sys::ServiceDomain::Bluetooth)) {
    bluetoothNotice("蓝牙正在处理，请稍候");return;
  }
  const auto state=sys::SystemService::bluetooth();
  const bool canConnect=state.enabled && state.state!=sys::BleHidState::Starting &&
      state.state!=sys::BleHidState::Connecting && !state.peer.connected;
  if(action==BtAction::Scan && state.scan==sys::BleScanState::Scanning) {
    sys::SystemService::cancelBluetoothScan();bluetoothNotice("正在停止搜索");return;
  }
  const bool canSwitch=state.enabled && state.state!=sys::BleHidState::Starting;
  if((action==BtAction::Scan && !canConnect) || (action==BtAction::Connect && !(btNearby?canConnect:canSwitch))) {
    bluetoothNotice("请开启蓝牙并先断开当前连接");return;
  }
  const auto revision=sys::SystemService::result(sys::ServiceDomain::Bluetooth).revision;
  auto service=sys::ServiceAction::BluetoothEnable;
  esp_err_t error=ESP_ERR_INVALID_ARG;
  switch(action) {
  case BtAction::Toggle:
    error=sys::SystemService::request(service,!state.enabled);break;
  case BtAction::Scan:
    service=sys::ServiceAction::BluetoothScan;
    error=sys::SystemService::request(service);
    if(error==ESP_OK) { btNearby=true;btListRevision=UINT32_MAX; }
    break;
  case BtAction::Disconnect:
    service=sys::ServiceAction::BluetoothDisconnect;
    error=sys::SystemService::request(service);break;
  case BtAction::Connect:
    if(btNearby) {
      if(index>=btScanned.size() || !btScanned[index].connectable) return;
      service=sys::ServiceAction::BluetoothConnect;
      error=sys::SystemService::connectKeyboard(btScanned[index]);
    } else {
      if(index>=btBonds.size() || !btBonds[index].keyboard) return;
      service=sys::ServiceAction::BluetoothReconnect;
      error=sys::SystemService::reconnectBond(btBonds[index]);
    }
    if(error==ESP_OK) btOwnConnection=true;
    break;
  case BtAction::Paired: return;
  }
  if(error!=ESP_OK) {
    const auto text=sys::SystemService::errorText(error);
    bluetoothNotice(text?text:"蓝牙请求失败，请重试");
  } else {
    btResultRevision=revision;btAction=service;btPending=true;btMessage.clear();
  }
  pollBluetooth();
}
void FcPlayer::pollBluetooth() {
  const auto state=sys::SystemService::bluetooth();
  const bool serviceBusy=sys::SystemService::busy(sys::ServiceDomain::Bluetooth);
  if(btPending && !serviceBusy) {
    const auto result=sys::SystemService::result(sys::ServiceDomain::Bluetooth);
    if(result.revision!=btResultRevision) {
      btPending=false;
      if(result.action==btAction && result.error!=ESP_OK) {
        const auto text=sys::SystemService::errorText(result.error);
        bluetoothNotice(text?text:"蓝牙操作失败，请重试");
      }
    }
  }
  const bool busy=btPending || serviceBusy;
  const bool connecting=state.cancellable();
  const bool starting=state.state==sys::BleHidState::Starting;
  const bool scanning=state.scan==sys::BleScanState::Scanning;
  const bool canConnect=state.enabled && !starting && !connecting && !state.peer.connected;
  c::checked(btToggle,state.enabled);c::disabled(btToggle,busy || starting);
  c::setText(lv_obj_get_child(btToggle,0),state.enabled?"蓝牙 开":"蓝牙 关");
  c::checked(btPaired,!btNearby);c::checked(btScan,btNearby);
  c::setText(lv_obj_get_child(btScan,0),scanning?"停止搜索":"搜索");
  c::disabled(btScan,busy || !canConnect);
  c::setText(lv_obj_get_child(btDisconnect,0),connecting?"取消":"断开");
  c::disabled(btDisconnect,busy || (!connecting && !state.peer.connected));
  btPeer.update(state.connecting()?state.target:state.peer,"未连接手柄或键盘");
  char error[64];std::snprintf(error,sizeof(error),"蓝牙连接失败 rc=%d，请重试",state.error);
  const char *text=busy?"蓝牙请求处理中":starting?"蓝牙启动中":
      state.hasError()?error:!state.enabled?"开启蓝牙后搜索手柄或键盘":
      state.peer.connected?(state.peer.keyboard?"输入设备已连接，可返回游戏":"当前连接为手机/电脑，请先断开"):
      connecting?"正在查找 / 配对输入设备":scanning?"搜索中，请让手柄进入配对模式":
      state.scan==sys::BleScanState::Failed?"搜索失败，请重试":"C档：游戏 · H档：设备\n长按配对3秒";
  const auto input=sys::BleKeyboardClient::diagnostics();
  using Stage=sys::BleKeyboardClient::Stage;
  if(connecting && input.stage!=Stage::Idle && input.stage!=Stage::Failed && input.stage!=Stage::Ready) {
    switch(input.stage) {
    case Stage::Service:text="已配对，查找输入服务…";break;
    case Stage::Characteristics:text="已配对，读取输入通道…";break;
    case Stage::ReportMap:text="已配对，识别按键格式…";break;
    case Stage::References:text="已配对，匹配按键通道…";break;
    case Stage::Subscribe:text="已配对，启用按键通知…";break;
    default:break;
    }
  }
  if(state.peer.connected && state.peer.keyboard)text="按住方向或 A / B，检查下方指示";
  if(!btMessage.empty() && lv_tick_elaps(btNoticeAt)<4000) text=btMessage.c_str();
  c::setText(status,text);
  if(state.peer.connected && state.peer.keyboard) {
    if(!btInput[0] && !btPressed && !lv_obj_is_scrolling(btList)) {
      lv_obj_clean(btList);lv_obj_set_flex_flow(btList,LV_FLEX_FLOW_COLUMN);
      btPackets=c::label(btList,"",238);
      lv_obj_set_style_text_font(btPackets,ui::theme::fontMenu(),0);
      lv_obj_set_height(btPackets,20);
      auto row=c::panel(btList,240,38);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
      const char *names[]={LV_SYMBOL_UP,LV_SYMBOL_DOWN,LV_SYMBOL_LEFT,LV_SYMBOL_RIGHT,"A","B"};
      for(unsigned i=0;i<6;++i) {btInput[i]=c::button(row,names[i],nullptr,nullptr,39,36);lv_obj_set_clickable(btInput[i],false);}
      btCode=c::label(btList,"",238);
      lv_obj_set_style_text_font(btCode,ui::theme::fontMenu(),0);
      lv_obj_set_height(btCode,20);
    }
    if(btInput[0]) {
      uint32_t revision=0;const auto keys=sys::KeyboardInput::snapshot(revision);
      const bool swapAB=sys::BleKeyboardClient::iineGamepad();
      const auto pad=uint8_t(sys::gamepad::fcPad(sys::gamepad::snapshot(),swapAB)|bluetoothKeyPad(keys));
      const uint8_t bits[]={0x10,0x20,0x40,0x80,1,2};
      const auto taps=sys::gamepad::fcPad(sys::gamepad::takePresses(),swapAB);
      for(unsigned i=0;i<6;++i)if(taps&bits[i])btInputUntil[i]=lv_tick_get()+250;
      for(unsigned i=0;i<6;++i)c::checked(btInput[i],(pad&bits[i])||(btInputUntil[i]&&int32_t(btInputUntil[i]-lv_tick_get())>0));
      char feedback[96];
      if(input.rejected)std::snprintf(feedback,sizeof(feedback),"收到 %lu 条 · %lu 条格式不匹配",static_cast<unsigned long>(input.received),static_cast<unsigned long>(input.rejected));
      else std::snprintf(feedback,sizeof(feedback),"变化 %lu · 报文 %lu",static_cast<unsigned long>(input.changes),static_cast<unsigned long>(input.received));
      c::setText(btPackets,feedback);
      char keyCode[32];std::snprintf(keyCode,sizeof(keyCode),"HID 0x%02X",unsigned(btLastKey));
      c::setText(btCode,btLastKey?keyCode:"");
    }
    return;
  }
  if(btInput[0]){lv_obj_clean(btList);btInput={};btPackets=btCode=nullptr;btDiagnosticKeys={};btInputUntil={};btLastKey=0;btListRevision=UINT32_MAX;}
  const uint32_t revision=btNearby?state.scanRevision:state.bondRevision;
  // Do not change a row's identity under a pressed finger or while scrolling.
  if(!btPressed && !lv_obj_is_scrolling(btList) && btListRevision!=revision) {
    try {
      if(btNearby) {
        std::vector<sys::BleScanDevice> next(sys::BleHid::kMaxScanDevices);
        next.resize(sys::SystemService::nearby(next.data(),next.size()));
        btScanned.swap(next);btBonds.clear();
      } else {
        std::vector<sys::BleBond> next;
        for(int i=0;i<state.bondCount;++i) if(state.bonds[i].keyboard) next.push_back(state.bonds[i]);
        btBonds.swap(next);btScanned.clear();
      }
    } catch(const std::bad_alloc &) {
      bluetoothNotice("列表内存不足，请返回后重试");return;
    }
    lv_obj_clean(btList);
    const size_t count=btNearby?btScanned.size():btBonds.size();
    if(!count) {
      auto empty=c::label(btList,btNearby?"暂无设备，请让手柄或键盘进入配对模式":"暂无配对设备，点击搜索连接",236);
      lv_label_set_long_mode(empty,LV_LABEL_LONG_MODE_WRAP);
    }
    for(size_t i=0;i<count;++i) {
      auto row=c::button(btList,"",[](lv_event_t *e) {
        auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
        const auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
        self->bluetoothAction(BtAction::Connect,index);
      },this,240,42);
      lv_obj_set_user_data(row,reinterpret_cast<void *>(i));
      lv_obj_add_event_cb(row,[](lv_event_t *e) {
        auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
        const auto code=lv_event_get_code(e);
        if(code==LV_EVENT_PRESSED) self->btPressed=true;
        if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) self->btPressed=false;
      },LV_EVENT_ALL,this);
      auto peer=btNearby?btScanned[i].address:btBonds[i];
      if(btNearby) std::snprintf(peer.name,sizeof(peer.name),"%s",btScanned[i].name);
      ui::BluetoothIdentity identity;identity.create(row,234,1,2);identity.update(peer);
    }
    btListRevision=revision;
  }
  const size_t count=btNearby?btScanned.size():btBonds.size();
  for(size_t i=0;i<count;++i)
    c::disabled(lv_obj_get_child(btList,i),busy || !state.enabled || starting ||
        (btNearby && (!canConnect || !btScanned[i].connectable)));
}
void FcPlayer::refresh() {
  if (page!=Page::List || job || (listing && !listing->done.load()))
    return;
  loadError.clear();
  listing = core::listMedia(true);
  c::setText(status, "正在读取游戏列表");
}
void FcPlayer::play(size_t index) {
  if (page!=Page::List || job || index >= files.size())
    return;
  pixels = static_cast<uint16_t *>(heap_caps_calloc(
      core::fc::Pixels, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) {
    c::setText(status, "显示内存不足");
    return;
  }
  job = core::fc::start(files[index].path,keySession);
  if (!job) {
    heap_caps_free(pixels);
    pixels = nullptr;
    c::setText(status, "游戏任务忙，请稍后重试");
    return;
  }
  page=Page::Game;
  if(listing) listing->cancelled.store(true);
  listing.reset();
  std::vector<core::FileEntry>{}.swap(files);
  sys::Buttons::gameMode(keySession,true);
  keyboardLinked=sys::BleHid::keyboardConnected();
  wasPaused=false;leaveApp=false;
  lv_timer_set_period(timer,16);
  lv_obj_clean(root());
  list = status = tiltToggle = nullptr;
  descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
  descriptor.header.w = core::fc::Width;
  descriptor.header.h = core::fc::Height;
  descriptor.header.stride = core::fc::Width * 2;
  descriptor.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
  descriptor.data_size = core::fc::Pixels * 2;
  descriptor.data = reinterpret_cast<const uint8_t *>(pixels);
  image = lv_image_create(root());
  lv_image_set_src(image, &descriptor);
  lv_obj_center(image);
  lv_obj_set_clickable(image,false);
  createControls();
  tiltHint=c::label(root(),"",150);
  lv_obj_set_pos(tiltHint,80,2);
  lv_obj_set_style_text_color(tiltHint,ui::theme::muted(),0);
  lv_obj_set_hidden(tiltHint,true);
  createMenu();
  clearInput();
  ui::ExternalKeyboard::capture(keys, this);
  blockedKeys={}; // Capture already fences the selection key until full release.
}
void FcPlayer::createMenu() {
  overlay = c::panel(root(), 240, 240);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
  lv_obj_set_gesture_bubble(overlay,false);
  pauseText = c::label(overlay, "正在加载游戏", 232);
  lv_obj_set_pos(pauseText, 4, 3);
  lv_obj_set_height(pauseText,52);
  lv_obj_set_style_text_line_space(pauseText,0,0);
  lv_label_set_long_mode(pauseText, LV_LABEL_LONG_MODE_WRAP);
  resume = c::button(
      overlay, "继续游戏",
      [](lv_event_t *e) {
        auto self = static_cast<FcPlayer *>(lv_event_get_user_data(e));
        self->resumeGame();
      },
      this, 240, 40);
  lv_obj_set_pos(resume, 0, 58);
  c::disabled(resume,true);
  tiltToggle=c::button(overlay,"重力 关",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->toggleTilt();
  },this,119,40);
  lv_obj_set_pos(tiltToggle,0,102);
  lv_obj_set_style_bg_color(tiltToggle,ui::theme::accent(),LV_STATE_CHECKED);
  joystickToggle=c::button(overlay,"摇杆:8F",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->toggleJoystick();
  },this,120,40);
  lv_obj_set_pos(joystickToggle,120,102);
  refreshControlOptions();
  saveEntry=c::button(overlay,"存档/读取",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->showSaves();
  },this,240,40);
  lv_obj_set_pos(saveEntry,0,146);c::disabled(saveEntry,true);
  auto exit = c::button(
      overlay, "返回游戏列表",
      [](lv_event_t *e) {
        auto self = static_cast<FcPlayer *>(lv_event_get_user_data(e));
        self->stopGame();
      },
      this, 119, 40);
  lv_obj_set_pos(exit, 0, 190);
  auto home=c::button(overlay,"退出应用",[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->stopGame(true);
  },this,120,40);
  lv_obj_set_pos(home,120,190);
}
void FcPlayer::refreshControlOptions() {
  const bool available=page==Page::List || (page==Page::Game && job &&
      job->ready.load(std::memory_order_acquire) && job->paused.load() &&
      !job->stop.load() && !job->done.load() && !savePanel && !saveBusy &&
      job->saves.stage.load()==core::fc::SaveStage::Idle);
  if(tiltToggle) {
    c::checked(tiltToggle,tiltEnabled);c::disabled(tiltToggle,!available);
    c::setText(lv_obj_get_child(tiltToggle,0),tiltEnabled?"重力 开":"重力 关");
  }
  if(joystickToggle) {
    c::disabled(joystickToggle,!available);
    c::setText(lv_obj_get_child(joystickToggle,0),touch.fourWay?"摇杆:4F":"摇杆:8F");
  }
}
void FcPlayer::toggleTilt() {
  if(page!=Page::List && (page!=Page::Game || !job || !job->ready.load() ||
      !job->paused.load() || job->stop.load() || job->done.load() || savePanel ||
      saveBusy || job->saves.stage.load()!=core::fc::SaveStage::Idle)) return;
  tiltEnabled=!tiltEnabled;
  releaseTilt();clearInput();refreshControlOptions();
  // Pause never starts sensor sampling; resume acquires the existing temporary lease.
}
void FcPlayer::toggleJoystick() {
  if(page!=Page::Game || !job || !job->ready.load() || !job->paused.load() ||
      job->stop.load() || job->done.load() || savePanel || saveBusy ||
      job->saves.stage.load()!=core::fc::SaveStage::Idle)return;
  touch.fourWay=!touch.fourWay;
  clearInput();refreshControlOptions();
}
void FcPlayer::createControls() {
  controls=c::panel(root(),padui::Screen,padui::Screen);
  lv_obj_set_style_bg_opa(controls,LV_OPA_TRANSP,0);
  lv_obj_set_clickable(controls,true);
  lv_obj_set_press_lock(controls,true);
  lv_obj_set_gesture_bubble(controls,false);
  lv_obj_add_event_cb(controls,touchEvent,LV_EVENT_ALL,this);
  stick=c::panel(controls,padui::Radius*2,padui::Radius*2);
  lv_obj_set_pos(stick,padui::StickX-padui::Radius,padui::StickY-padui::Radius);
  lv_obj_set_style_radius(stick,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(stick,lv_color_black(),0);
  lv_obj_set_style_bg_opa(stick,LV_OPA_20,0);
  lv_obj_set_style_border_color(stick,lv_color_white(),0);
  lv_obj_set_style_border_width(stick,2,0);
  lv_obj_set_style_border_opa(stick,LV_OPA_40,0);
  thumb=c::panel(controls,padui::ThumbRadius*2,padui::ThumbRadius*2);
  lv_obj_set_style_radius(thumb,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(thumb,lv_color_white(),0);
  lv_obj_set_style_bg_opa(thumb,LV_OPA_40,0);
  for(auto decoration:{stick,thumb}) lv_obj_set_clickable(decoration,false);
  updateStick();
}
void FcPlayer::updateStick() {
  if(!thumb) return;
  const bool external=sys::BleHid::gamepadConnected();
  lv_obj_set_hidden(stick,external);lv_obj_set_hidden(thumb,external);
  if(external) return;
  int dx=touch.dx,dy=touch.dy;
  const int travel=padui::Radius-padui::ThumbRadius;
  if(tiltEnabled || touch.fourWay) {
    // Display the actual composed direction, including keyboard/touch overrides.
    dx=((lastPad&padui::Right)?travel:0)-((lastPad&padui::Left)?travel:0);
    dy=((lastPad&padui::Down)?travel:0)-((lastPad&padui::Up)?travel:0);
  }
  const float length=std::sqrt(float(dx*dx+dy*dy));
  if(length>travel) { dx=int(dx*travel/length);dy=int(dy*travel/length); }
  lv_obj_set_pos(thumb,padui::StickX-padui::ThumbRadius+dx,
                       padui::StickY-padui::ThumbRadius+dy);
}
void FcPlayer::releaseTilt() {
  sys::Qmi8658::releaseTemporaryUse(tiltSession);tiltSession=0;
  tilt.clear();tiltReady=tiltSampleSeen=false;
  if(tiltHint) lv_obj_set_hidden(tiltHint,true);
}
void FcPlayer::pollTilt() {
  if(!tiltEnabled || !job || !job->ready.load(std::memory_order_acquire) ||
     job->paused.load() || job->stop.load() || !sys::Power::isScreenOn() || sys::Power::sleepPending()) {
    releaseTilt();return;
  }
  if(!tiltSession) {
    tiltSession=sys::Qmi8658::claimTemporaryUse();
    tiltStartedAt=static_cast<uint32_t>(esp_timer_get_time()/1000);
    tilt.clear();tiltReady=tiltSampleSeen=false;
  }
  const uint8_t previousPad=tilt.pad;
  const auto previousDirection=touch.fourWay?padui::fourDirections(tilt.pad,tilt.x,tilt.y):tilt.pad;
  const bool previousReady=tiltReady;
  sys::AccelerationSample sample;
  const bool sampled=sys::Qmi8658::temporaryAcceleration(tiltSession,sample);
  if(sampled)sys::ScreenOrientation::mapAcceleration(sample.value);
  // Timestamp after the cache copy: a concurrent newer sample must not look
  // UINT32_MAX milliseconds old when the GUI was preempted before the copy.
  const auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
  if(sampled && tiltui::fresh(now,sample.timeMs)) {
    if(!tiltSampleSeen || sample.timeMs!=tiltSampleAt) {
      tiltReady=tilt.update(sample.value.x,sample.value.y,sample.value.z);
      tiltSampleAt=sample.timeMs;tiltSampleSeen=true;
    }
  } else if(!tiltSampleSeen || !tiltui::fresh(now,tiltSampleAt) || !sys::Qmi8658::enabled()) {
    tilt.clear();tiltReady=tiltSampleSeen=false;
  }
  if(tiltHint) {
    if(tiltReady) lv_obj_set_hidden(tiltHint,true);
    else {
      c::setText(tiltHint,!sys::Settings::motionSensorOn()?"请开启重力感应":uint32_t(now-tiltStartedAt)<1500?"感应准备中":"感应不可用");
      lv_obj_set_hidden(tiltHint,false);
    }
  }
  const auto direction=touch.fourWay?padui::fourDirections(tilt.pad,tilt.x,tilt.y):tilt.pad;
  if(previousPad!=tilt.pad || previousReady!=tiltReady || previousDirection!=direction) sendInput();
}
void FcPlayer::touchEvent(lv_event_t *e) {
  auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
  const auto code=lv_event_get_code(e);
  if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING &&
     code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
  if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
    self->touch.clear(); self->updateStick(); self->sendInput(); return;
  }
  auto indev=lv_indev_active();
  if(!indev || !self->job || !self->job->ready.load(std::memory_order_acquire) ||
     self->job->paused.load() || self->job->stop.load() ||
     !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
  lv_point_t point;lv_indev_get_point(indev,&point);
  lv_area_t bounds;lv_obj_get_coords(self->controls,&bounds);
  const int x=point.x-bounds.x1,y=point.y-bounds.y1;
  if(code==LV_EVENT_PRESSED) self->touch.begin(x,y);
  else self->touch.move(x,y);
  if(self->touch.active==padui::Region::Menu) {
    lv_indev_wait_release(indev);self->showMenu();return;
  }
  if(sys::BleHid::gamepadConnected() && self->touch.active==padui::Region::Stick) {
    self->touch.clear();lv_indev_wait_release(indev);
  }
  self->updateStick();self->sendInput();
}
void FcPlayer::sendInput() {
  if(!job) return;
  const bool active=job->ready.load(std::memory_order_acquire) && !job->paused.load() &&
      !job->stop.load() && sys::Power::isScreenOn() && !sys::Power::sleepPending();
  const auto gravity=touch.fourWay?padui::fourDirections(tilt.pad,tilt.x,tilt.y):tilt.pad;
  const uint8_t pad=active?tiltui::compose(tiltEnabled,gravity,touch.pad,
      touch.active==padui::Region::Stick,padui::combine(gamepadPad,keyboardPad)):0;
  job->buttons.store(pad);
  if(active) {
    // The current direction replaces any queued earlier turn.
    // Keep a released tap until consumed, and preserve A/B/Select/Start edges.
    if(pad&0xf0)job->pressed.fetch_and(uint8_t(0x0f));
    job->pressed.fetch_or(uint8_t(pad & ~lastPad));
  }
  else job->pressed.store(0);
  lastPad=pad;
  if(tiltEnabled || touch.fourWay) updateStick();
}
void FcPlayer::clearInput() {
  keyboardPad=gamepadPad=lastPad=0;previous={};
  gamepadGate.clear(sys::gamepad::snapshot());(void)sys::gamepad::takePresses();
  tilt.clear();tiltReady=tiltSampleSeen=false;
  uint32_t revision=0;blockedKeys=sys::KeyboardInput::snapshot(revision);
  touch.clear();updateStick();
  sys::Buttons::clearGameKeys(keySession);
  if(job) { job->buttons.store(0);job->pressed.store(0); }
}
void FcPlayer::showMenu() {
  if(!job || (wasPaused && job->paused.load())) return;
  job->setPaused(true);
  releaseTilt();
  wasPaused=true;
  sys::Buttons::gameRunning(keySession,false);
  clearInput();
  if(overlay) lv_obj_set_hidden(overlay,false);
  refreshControlOptions();
}
void FcPlayer::resumeGame() {
  if(!job || savePanel || saveBusy || job->saves.stage.load()!=core::fc::SaveStage::Idle ||
     !job->ready.load(std::memory_order_acquire) || job->stop.load() ||
     job->done.load() || !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
  clearInput();wasPaused=false;job->setPaused(false);
}
void FcPlayer::stopGame(bool home) {
  if(!job) return;
  leaveApp=home;showMenu();job->requestStop();
  if(home)ui::WindowManager::instance().goHome(); // worker owns unfinished IO
}
void FcPlayer::keys(const sys::hidinput::Keys &keys, void *context) {
  auto self = static_cast<FcPlayer *>(context);
  if (!self->job || !self->job->ready.load(std::memory_order_acquire) ||
      self->job->done.load())
    return;
  if(!sys::Power::isScreenOn() || sys::Power::sleepPending()) {
    if(!self->job->paused.load()) self->showMenu();
    return;
  }
  const bool linked=sys::BleHid::keyboardConnected();
  if(self->keyboardLinked!=linked) { self->keyboardLinked=linked;self->clearInput(); }
  if(!linked) {
    // ExternalKeyboard sends an empty snapshot every poll while disconnected.
    // It must not keep clearing a held joystick or the physical A/B state.
    return;
  }
  auto effective=keys;
  for(size_t i=0;i<effective.bits.size();++i) {
    self->blockedKeys.bits[i]&=keys.bits[i];
    effective.bits[i]&=uint8_t(~self->blockedKeys.bits[i]);
  }
  // H mode belongs to system navigation even while FC owns a raw handler.
  if(sys::BleKeyboardClient::iineKeyboard()) {
    self->keyboardPad=0;self->previous={};self->sendInput();return;
  }
  const auto &map = self->job->mapping;
  const auto pause = map.usage[core::fc::Pause],
             exit = map.usage[core::fc::Exit];
  if (effective.down(exit) && !self->previous.down(exit)) {
    if(self->savePanel) self->closeSaves();else self->stopGame();return;
  }
  if (effective.down(pause) && !self->previous.down(pause)) {
    if(self->job->paused.load()) self->resumeGame();else self->showMenu();
    return;
  }
  self->keyboardPad=map.pad(effective);
  self->sendInput();self->previous=effective;
}
void FcPlayer::pollGamepad() {
  if(!job)return;
  const bool linked=sys::BleHid::gamepadConnected();
  if(gamepadLinked!=linked){gamepadLinked=linked;clearInput();}
  const auto presses=sys::gamepad::takePresses();
  const auto state=sys::gamepad::snapshot();
  const bool active=linked&&job->ready.load()&&!job->paused.load()&&!job->stop.load()&&
      !job->done.load()&&sys::Power::isScreenOn()&&!sys::Power::sleepPending();
  if(active) {
    const auto allowed=sys::gamepad::State::unpack(presses.packed()&~gamepadGate.blocked);
    const bool swapAB=sys::BleKeyboardClient::iineGamepad();
    gamepadPad=sys::gamepad::fcPad(gamepadGate.filter(state),swapAB);
    sendInput();
    // Preserve quick button taps. Use the current direction when held; only
    // replay a released direction tap when all direction sources are neutral.
    const auto taps=sys::gamepad::fcPad(allowed,swapAB);
    if(!(lastPad&0xf0)&&(taps&0xf0)) {
      job->pressed.fetch_and(uint8_t(0x0f));
      job->pressed.fetch_or(taps);
    } else job->pressed.fetch_or(uint8_t(taps&0x0f));
  } else {
    gamepadPad=0;gamepadGate.clear(state);sendInput();
  }
}
void FcPlayer::poll() {
  if(page==Page::Bluetooth) { pollBluetooth();return; }
  if (page==Page::List && listing && listing->done.load(std::memory_order_acquire)) {
    files = std::move(listing->entries);
    lv_obj_clean(list);
    if (listing->error)
      c::setText(status, core::storageError(listing->error));
    else if(!loadError.empty())
      c::setText(status,loadError.c_str());
    else
      c::setText(status, listing->truncated ? "只显示前256个游戏"
                         : files.empty()    ? "将.nes文件放入nes"
                                            : "选择游戏，可直接开始");
    for (size_t i = 0; i < files.size(); ++i) {
      auto row = c::button(
          list, files[i].name.c_str(),
          [](lv_event_t *e) {
            auto self = static_cast<FcPlayer *>(
                lv_obj_get_user_data(lv_event_get_target_obj(e)));
            self->play(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
          },
          reinterpret_cast<void *>(i), 240, 40);
      lv_obj_set_user_data(row, this);
      auto text = lv_obj_get_child(row, 0);
      lv_obj_set_width(text, 230);
      lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    }
    listing.reset();
  }
  if (!job)
    return;
  if (job->done.load(std::memory_order_acquire)) {
    const std::string error = job->error;
    releaseTilt();
    job.reset();
    if(leaveApp) { ui::WindowManager::instance().goHome();return; }
    showList();
    loadError=error; // Keep the reason after the asynchronous list refresh.
    if (!error.empty())
      c::setText(status, error.c_str());
    return;
  }
  if (!job->ready.load(std::memory_order_acquire))
    return;
  pollSaves();
  if (!sys::Power::isScreenOn() || sys::Power::sleepPending()) {
    showMenu();
    lv_timer_set_period(timer,250);
    return;
  }
  const bool linked=sys::BleHid::keyboardConnected();
  if(keyboardLinked!=linked) { keyboardLinked=linked;clearInput(); }
  const bool paused=job->paused.load();
  if(wasPaused!=paused) { wasPaused=paused;clearInput(); }
  pollGamepad();
  lv_timer_set_period(timer,job->paused.load()?50:16);
  c::disabled(resume,!job->ready.load() || job->stop.load() || savePanel || saveBusy);
  c::disabled(saveEntry,!job->ready.load() || job->stop.load());
  refreshControlOptions();
  if (job->paused.load() || job->stop.load()) {
    releaseTilt();
    lv_obj_set_hidden(overlay,false);
    c::setText(pauseText,
               job->stop.load()           ? "正在退出游戏"
               : !pauseNotice.empty() && uint32_t(lv_tick_get()-pauseNoticeAt)<5000 ? pauseNotice.c_str()
               : job->saves.autoFailed.load() ? "自动存档失败\n请检查SD卡或可用空间"
               : !job->warning.empty()    ? job->warning.c_str()
               : gamepadLinked           ? "游戏菜单 · 蓝牙手柄"
               : keyboardLinked          ? "游戏菜单 · 蓝牙键盘\n空格继续，Esc返回列表"
                                          : "游戏菜单\nKEY=A · BOOT=B");
  } else {
    pollTilt();
    // Running gameplay is active use, even when waiting between key presses.
    // The screen-on check above preserves explicit PWR and deep sleep.
    lv_display_trigger_activity(nullptr);
    lv_obj_set_hidden(overlay,true);
  }
  bool updated=false;
  {
    const std::unique_lock<std::mutex> lock(job->frameMutex, std::try_to_lock);
    if (lock.owns_lock() && job->frame && job->framePending.load(std::memory_order_acquire)) {
      std::memcpy(pixels, job->frame, core::fc::Pixels * 2);
      job->framePending.store(false,std::memory_order_release);
      updated=true;
    }
  } // No image cache/UI work while holding the producer's mailbox lock.
  if(updated) {
    lv_image_cache_drop(&descriptor);
    lv_obj_invalidate(image);
  }
}
bool FcPlayer::onBack() {
  if(savePanel) { closeSaves();return true; }
  if(page==Page::Bluetooth) { showList();return true; }
  if (!job)
    return false;
  showMenu();
  return true;
}
void FcPlayer::onHide() {
  releaseTilt();
  sys::Buttons::releaseGameKeys(keySession);keySession=0;
  clearInput();
  if(page==Page::Bluetooth) closeBluetooth();
  ui::ExternalKeyboard::capture(nullptr, nullptr);
  if (timer)
    lv_timer_pause(timer);
  if (job)
    job->requestStop();
  if (listing)
    listing->cancelled.store(true);
}
void FcPlayer::onDestroy() {
  onHide();
  sys::EventBus::unsubscribe(screenOffSub);screenOffSub=0;
  sys::EventBus::unsubscribe(screenOnSub);screenOnSub=0;
  releaseImage();
  if (timer)
    lv_timer_delete(timer);
  timer = nullptr;
  job.reset();
  listing.reset();
  files.clear();
}
} // namespace apps
