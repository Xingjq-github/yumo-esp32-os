#pragma once
#include "app.hpp"
#include "core/connectivity.hpp"
#include "core/system_service.hpp"
#include "core/storage_browser.hpp"
#include "ui/bluetooth_identity.hpp"
#include "ui/focus_pager.hpp"
#include <array>
#include <string>
namespace apps {
class Settings : public ui::App {
public:
    const char *name() const override { return "Settings"; }
    const char *icon() const override { return LV_SYMBOL_SETTINGS; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
    bool handlesHorizontalGesture() const override {return ui::theme::focus() && page==Page::Root;}
private:
    enum class Page { Root, Wifi, WifiSaved, WifiNetwork, Bluetooth, BluetoothDevice, BluetoothScan, Display, System, Time, About, Theme };
    enum class Dialog { None, ForgetWifi, ForgetBond, ForgetAll, PowerOff, DeepSleep, Theme, UsbFlash };
    struct Views {
        lv_obj_t *page=nullptr,*body=nullptr,*status=nullptr,*list=nullptr,*toggle=nullptr;
        lv_obj_t *wifiSummary=nullptr,*btSummary=nullptr,*scanButton=nullptr,*discoverButton=nullptr;
        lv_obj_t *ssid=nullptr,*password=nullptr;
        lv_obj_t *connect=nullptr,*forget=nullptr,*brightness=nullptr,*volume=nullptr,*brightnessValue=nullptr,*volumeValue=nullptr;
        lv_obj_t *sysinfo=nullptr,*motion=nullptr,*battery=nullptr;
        lv_obj_t *sdEject=nullptr,*sdHelp=nullptr;
        lv_obj_t *usbMode=nullptr,*usbStatus=nullptr;
        lv_obj_t *castToggle=nullptr,*castScreenOff=nullptr,*castStatus=nullptr;
        lv_obj_t *quietBrightness=nullptr,*quietValue=nullptr,*screenTime=nullptr,*quietTime=nullptr;
        lv_obj_t *autoSleep=nullptr,*protectLinks=nullptr,*standbyTime=nullptr,*deepTime=nullptr,*powerStrategy=nullptr;
        lv_obj_t *sleepOnPower=nullptr,*screenOffOnPower=nullptr,*supplyStatus=nullptr,*confirmBattery=nullptr;
        lv_obj_t *aboutUptime=nullptr,*aboutCpu=nullptr,*aboutPower=nullptr;
        lv_obj_t *aboutSupply=nullptr,*aboutWifi=nullptr,*aboutBluetooth=nullptr;
        lv_obj_t *aboutMotion=nullptr,*aboutSd=nullptr,*aboutUsb=nullptr,*aboutCast=nullptr;
        lv_obj_t *aboutMemoryDetail=nullptr;
        lv_obj_t *aboutWifiIp=nullptr,*aboutBluetoothAddress=nullptr,*aboutEvents=nullptr;
        struct StorageCard {
            lv_obj_t *total=nullptr,*free=nullptr,*note=nullptr;
            std::array<lv_obj_t *,5> segments{},details{};
        };
        std::array<StorageCard,4> aboutStorage{};
        lv_obj_t *aboutStorageRefresh=nullptr;
        std::array<lv_obj_t *,6> date{};
        lv_obj_t *timeSyncButton=nullptr,*timeStatus=nullptr;
        std::array<lv_obj_t *,sys::BleHid::kMaxScanDevices> scanRows{};
        std::array<ui::BluetoothIdentity,sys::BleHid::kMaxScanDevices> scanIdentity{};
        ui::BluetoothIdentity peerIdentity;
        lv_obj_t *btAddress=nullptr;
    } v;
    Page page=Page::Root,nextPage=Page::Root;
    bool pagePending=false,listPressed=false,noticePending=false;
    Dialog dialog=Dialog::None,pendingDialog=Dialog::None;
    int dialogAnswer=0;
    lv_obj_t *modal=nullptr,*toast=nullptr;
    lv_timer_t *timer=nullptr;
    std::shared_ptr<core::StorageJob> sdRead;
    std::shared_ptr<core::StorageJob> aboutStorageRead;
    core::StorageUsage aboutUsage;
    uint32_t aboutSdEpoch=0;
    uint32_t lastRefresh=0,toastUntil=0,scanRevision=UINT32_MAX,bondRevision=UINT32_MAX;
    uint32_t aboutRefreshed=0;
    uint32_t timeRevision=UINT32_MAX;
    std::array<uint32_t,3> resultRevisions{};
    std::string selectedSsid,selectedPassword,noticeText,dialogText;
    bool manualWifiEntry=false;
    uint8_t selectedTheme=10;
    bool themeSubmitted=false;
    ui::FocusPager focusMenu;
    size_t focusIndex=0;
    sys::BleBond selectedBond{};
    std::array<wifi_ap_record_t,sys::Connectivity::kMaxNetworks> aps{};
    std::array<std::string,sys::Connectivity::kMaxNetworks> networkNames;
    size_t networkCount=0;
    std::array<sys::BleBond,sys::BlePeerStore::Capacity> bonds{};
    int bondCount=0,scanShown=0;
    std::array<sys::BleScanDevice,sys::BleHid::kMaxScanDevices> scanned{};
    void navigate(Page target);
    void buildPage();
    void buildRoot();
    void refreshFocusRoot();
    void buildTheme();
    void buildWifi();
    void buildSavedWifi();
    void buildNetwork();
    void refreshWifi(bool force);
    void refreshWifiList();
    void buildBluetooth();
    void buildBluetoothDevice();
    void buildBluetoothScan();
    void refreshBluetooth(bool force);
    void refreshBonds(const sys::BluetoothSnapshot &state);
    void buildDisplay();
    void buildSystem();
    void buildSupplyButton();
    void buildSupplyHelp();
    void refreshSupply();
    lv_obj_t *powerTime(const char *title,uint16_t seconds);
    static void powerOptionCb(lv_event_t *e);
    void refreshSd();
    void buildTime();
    void refreshTime(bool force);
    void buildAbout();
    void refreshAbout();
    void buildStorageOverview();
    void refreshStorageOverview();
    static void storageRefreshCb(lv_event_t *e);
    void saveTime();
    void refresh(bool force=false);
    void tick();
    void notice(const char *message);
    void report(esp_err_t error,const char *success=nullptr);
    void ask(Dialog kind,const char *message);
    void showDialog();
    void closeDialog();
    void processDialog();
    void request(sys::ServiceAction action,bool value=false);
    lv_obj_t *menuRow(const char *title,Page target,lv_obj_t **detail=nullptr);
    void markListTouch(lv_obj_t *row);
    static void timerCb(lv_timer_t *timer);
    static void pageCb(lv_event_t *e);
    static void backCb(lv_event_t *e);
    static void wifiToggleCb(lv_event_t *e);
    static void wifiScanCb(lv_event_t *e);
    static void networkCb(lv_event_t *e);
    static void connectCb(lv_event_t *e);
    static void forgetWifiCb(lv_event_t *e);
    static void btToggleCb(lv_event_t *e);
    static void discoverCb(lv_event_t *e);
    static void btScanCb(lv_event_t *e);
    static void bondCb(lv_event_t *e);
    static void nearbyCb(lv_event_t *e);
    static void forgetBondCb(lv_event_t *e);
    static void forgetAllCb(lv_event_t *e);
    static void sliderCb(lv_event_t *e);
    static void systemToggleCb(lv_event_t *e);
};
} // namespace apps
