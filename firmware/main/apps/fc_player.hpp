#pragma once
#include "ui/app_icons.hpp"
#include "core/fc_emulator.hpp"
#include "core/fc_controls.hpp"
#include "core/fc_tilt.hpp"
#include "core/gamepad_input.hpp"
#include "core/storage_browser.hpp"
#include "core/system_service.hpp"
#include "ui/bluetooth_identity.hpp"
#include "ui/app.hpp"
namespace apps {
class FcPlayer final : public ui::App {
public:
  const char *name() const override { return "FC游戏"; }
  const char *icon() const override { return ui::icons::Fc; }
  bool handlesHorizontalGesture() const override { return true; }
  bool locksScreenOrientation() const override {return true;}
  bool allowsQuickPanel() const override {
    return page!=Page::Game || (job && job->paused.load() && overlay &&
      !lv_obj_is_hidden(overlay) && !saveBusy);
  }
  void onCreate(lv_obj_t *) override;
  void onHide() override;
  void onDestroy() override;
  bool onBack() override;
  uint32_t powerHints() const override {return job?sys::power::KeepSession|(page==Page::Game && !job->paused.load()?uint32_t(sys::power::StayLit):0U):0;}

private:
  enum class Page { List, Bluetooth, Game };
  enum class BtAction { Toggle, Scan, Disconnect, Paired, Connect };
  void poll();
  void showBluetooth();
  void closeBluetooth();
  void pollBluetooth();
  void bluetoothAction(BtAction action,size_t index=0);
  void bluetoothNotice(const char *text);
  static void bluetoothKeys(const sys::hidinput::Keys &,void *);
  static uint8_t bluetoothKeyPad(const sys::hidinput::Keys &);
  void showList();
  void refresh();
  void play(size_t);
  void releaseImage();
  void createControls();
  void createMenu();
  void toggleTilt();
  void toggleJoystick();
  void refreshControlOptions();
  void updateStick();
  void pollTilt();
  void releaseTilt();
  void sendInput();
  void pollGamepad();
  void clearInput();
  void showSaves();
  void closeSaves();
  void updateSaves();
  void pollSaves();
  void confirmSave();
  void saveNotice(const char *,bool confirm);
  void submitSave(core::fc::SaveAction);
  void showMenu();
  void resumeGame();
  void stopGame(bool home=false);
  static void touchEvent(lv_event_t *);
  static void keys(const sys::hidinput::Keys &, void *);
  lv_obj_t *saveEntry=nullptr,*savePanel=nullptr,*saveDialog=nullptr,*saveTitle=nullptr,
           *saveList=nullptr,*saveBack=nullptr,*saveRefresh=nullptr,*saveWrite=nullptr,*saveRead=nullptr;
  std::array<lv_obj_t *,FC_SAVE_SLOTS> saveRows{},saveDates{},saveTimes{};
  std::array<fc_save_slot_t,FC_SAVE_SLOTS> saveSlots{};
  unsigned saveSelection=0;
  uint64_t saveGeneration=0;
  bool saveBusy=false,saveListReady=false;
  std::string pauseNotice;
  uint32_t pauseNoticeAt=0;
  Page page=Page::List;
  bool btNearby=false, btPressed=false, btPending=false, btOwnConnection=false;
  uint32_t btListRevision=UINT32_MAX, btResultRevision=0, btNoticeAt=0;
  sys::ServiceAction btAction=sys::ServiceAction::BluetoothEnable;
  std::string btMessage,loadError;
  // Only allocated on the Bluetooth page; released before gameplay/exit.
  std::vector<sys::BleBond> btBonds;
  std::vector<sys::BleScanDevice> btScanned;
  lv_obj_t *btToggle=nullptr, *btScan=nullptr, *btDisconnect=nullptr,
           *btPaired=nullptr, *btList=nullptr;
  std::array<lv_obj_t *,6> btInput{};
  std::array<uint32_t,6> btInputUntil{};
  sys::hidinput::Keys btDiagnosticKeys;
  uint8_t btLastKey=0;
  lv_obj_t *btPackets=nullptr,*btCode=nullptr;
  ui::BluetoothIdentity btPeer;
  lv_timer_t *timer = nullptr;
  lv_obj_t *status = nullptr, *list = nullptr, *image = nullptr,
           *overlay = nullptr, *pauseText = nullptr, *resume = nullptr,
           *controls = nullptr, *stick = nullptr, *thumb = nullptr;
  lv_image_dsc_t descriptor{};
  uint16_t *pixels = nullptr;
  sys::hidinput::Keys previous;
  sys::hidinput::Keys blockedKeys;
  core::fc::controls::Touch touch;
  core::fc::tilt::State tilt;
  bool tiltEnabled=false,tiltReady=false,tiltSampleSeen=false;
  uint32_t tiltSession=0,tiltSampleAt=0,tiltStartedAt=0;
  lv_obj_t *tiltToggle=nullptr,*tiltHint=nullptr,*joystickToggle=nullptr;
  uint32_t keySession=0,screenOffSub=0,screenOnSub=0;
  uint8_t keyboardPad=0,gamepadPad=0,lastPad=0;
  sys::gamepad::Gate gamepadGate;
  bool gamepadLinked=false;
  bool keyboardLinked=false,wasPaused=true,leaveApp=false;
  std::vector<core::FileEntry> files;
  std::shared_ptr<core::StorageJob> listing;
  std::shared_ptr<core::fc::Job> job;
};
} // namespace apps
