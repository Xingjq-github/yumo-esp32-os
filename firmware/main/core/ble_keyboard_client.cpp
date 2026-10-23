#include "ble_keyboard_client.hpp"
#include "ble_hid.hpp"
#include "keyboard_input.hpp"
#include "hid_gamepad_report.hpp"
#include "hid_iine_profile.hpp"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <new>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace sys {
namespace {
constexpr char tag[]="ble_keyboard";
constexpr uint16_t invalid=BLE_HS_CONN_HANDLE_NONE;
uint16_t connection=invalid,serviceStart=0,serviceEnd=0,mapHandle=0,protocol=0,bootHandle=0;
uintptr_t generation=0;
std::atomic<bool> isReady{false};
std::atomic<bool> keyboardAvailable{false},gamepadAvailable{false};
std::atomic<bool> iineProfile{false},iinePadProfile{false};
std::atomic<bool> diagnosticVisible{false},mapTracePending{false};
std::atomic<unsigned> traceRemaining{0},traceOtherRemaining{0};
std::atomic<uint32_t> traceSession{0};
struct Characteristic { uint16_t declaration=0,value=0,uuid=0;uint8_t properties=0; };
struct Input {
    uint16_t value=0,end=0,cccd=0,reference=0;uint8_t id=0;
    bool enabled=false,seen=false,inputReport=false;hidinput::Keys keys;gamepad::State pad;
    uint32_t traceSession=0;size_t traceLength=0;std::array<uint8_t,64> traceData{};
};
struct Workspace {
    std::array<Characteristic,32> chars{};
    std::array<Input,8> inputs{};
    std::array<uint8_t,2048> mapBytes{};
    hidinput::ReportMap map;
    hidinput::GamepadReportMap gamepadMap;
};
// Task-context parsing buffers need no DMA/internal RAM. Retain a bounded
// PSRAM workspace for the boot lifetime; never free it under GATT callbacks.
Workspace *workspace=nullptr;
unsigned charCount=0,inputCount=0,cursor=0,subscribed=0;size_t mapLength=0;
bool bootMode=false;
bool keyboardMap=false,padMap=false;
using Stage=BleKeyboardClient::Stage;
std::atomic<Stage> stage{Stage::Idle};
std::atomic<uint32_t> received{0},rejected{0},changes{0};
std::atomic<int> lastError{0};
void traceBytes(const char *kind,const uint8_t *data,size_t length,bool requireView=true) {
    for(size_t offset=0;offset<length && (!requireView || diagnosticVisible.load());offset+=24) {
        char hex[73]{};const size_t n=length-offset<24?length-offset:24;
        for(size_t i=0;i<n;++i)std::snprintf(hex+i*3,4,"%02x ",data[offset+i]);
        ESP_LOGI(tag,"FC HID %s offset=%u %s",kind,unsigned(offset),hex);
    }
}
void traceMap() {
    if(diagnosticVisible.load() && mapTracePending.exchange(false))
        traceBytes("map",workspace->mapBytes.data(),mapLength);
}
const char *stageName(Stage s) {
    switch(s){case Stage::Service:return "service";case Stage::Characteristics:return "characteristics";
    case Stage::ReportMap:return "report-map";case Stage::References:return "report-reference";
    case Stage::Subscribe:return "subscribe";case Stage::Ready:return "ready";case Stage::Failed:return "failed";default:return "idle";}
}
void *token() { return reinterpret_cast<void *>(generation); }
bool valid(uint16_t handle,void *arg) { return handle==connection && connection!=invalid && reinterpret_cast<uintptr_t>(arg)==generation; }
void fail(int error) {
    if(connection==invalid) return;
    ESP_LOGW(tag,"HID discovery failed stage=%s rc=%d reports=%u subscribed=%u map_bytes=%u",stageName(stage.load()),error,inputCount,subscribed,unsigned(mapLength));
    lastError=error;stage=Stage::Failed;
    const auto failed=connection;++generation;connection=invalid;
    isReady.store(false);keyboardAvailable=false;gamepadAvailable=false;KeyboardInput::reset();gamepad::reset();
    BleHid::keyboardDiscoveryFinished(false,error);
    (void)ble_gap_terminate(failed,BLE_ERR_REM_USER_CONN_TERM);
}
void check(int rc) { if(rc) fail(rc); }
bool mapped(const Input &in) {return bootMode || (keyboardMap&&workspace->map.has(in.id)) || (padMap&&workspace->gamepadMap.has(in.id));}
void traceSample(Input &in,const uint8_t *data,size_t length) {
    if(!diagnosticVisible.load() || length>in.traceData.size())return;
    const auto session=traceSession.load();
    if(in.traceSession==session && in.traceLength==length && !std::memcmp(in.traceData.data(),data,length))return;
    auto &remaining=mapped(in)?traceRemaining:traceOtherRemaining;
    auto budget=remaining.load();
    while(budget && !remaining.compare_exchange_weak(budget,budget-1)){}
    if(!budget || !diagnosticVisible.load())return;
    in.traceSession=session;in.traceLength=length;std::memcpy(in.traceData.data(),data,length);
    ESP_LOGI(tag,"FC HID sample handle=%u id=%u bytes=%u",in.value,in.id,unsigned(length));
    traceBytes("input",data,length);
}
void nextInput();
int subscribedCb(uint16_t handle,const ble_gatt_error *error,ble_gatt_attr *,void *arg) {
    if(!valid(handle,arg)) return 0;
    auto &in=workspace->inputs[cursor];
    if(error->status) {
        // Optional, unsupported input reports must not disable a usable keyboard.
        if(mapped(in)){fail(error->status);return 0;}
        ESP_LOGW(tag,"HID optional notify failed handle=%u id=%u rc=%d",in.value,in.id,error->status);
        ++cursor;nextInput();return 0;
    }
    in.enabled=true;
    ESP_LOGI(tag,"HID notify enabled handle=%u cccd=%u id=%u mapped=%d",in.value,in.cccd,in.id,int(mapped(in)));
    if(bootMode||(keyboardMap&&workspace->map.has(in.id)))keyboardAvailable=true;
    if(padMap&&workspace->gamepadMap.has(in.id))gamepadAvailable=true;
    ++subscribed;++cursor;nextInput();return 0;
}
void subscribe() {
    if(!workspace->inputs[cursor].cccd) { ++cursor;nextInput();return; }
    const uint8_t notify[]={1,0};
    stage=Stage::Subscribe;
    check(ble_gattc_write_flat(connection,workspace->inputs[cursor].cccd,notify,sizeof(notify),subscribedCb,token()));
}
int referenceCb(uint16_t handle,const ble_gatt_error *error,ble_gatt_attr *attr,void *arg) {
    if(!valid(handle,arg)) return 0;
    if(error->status || !attr || OS_MBUF_PKTLEN(attr->om)!=2) { fail(error->status?error->status:BLE_HS_EBADDATA);return 0; }
    uint8_t value[2]{};ble_hs_mbuf_to_flat(attr->om,value,2,nullptr);
    workspace->inputs[cursor].id=value[0];
    workspace->inputs[cursor].inputReport=value[1]==1;
    ESP_LOGI(tag,"HID reference handle=%u id=%u type=%u keyboard=%d gamepad=%d",workspace->inputs[cursor].value,value[0],value[1],int(keyboardMap&&workspace->map.has(value[0])),int(padMap&&workspace->gamepadMap.has(value[0])));
    // Match the ESP-IDF HID-host listener strategy: enable all input channels.
    // Multi-function controllers may expose additional channels absent from the
    // current mode's report map. Decode only mapped reports after notification.
    if(value[1]!=1) { ++cursor;nextInput();return 0; }
    subscribe();return 0;
}
int descriptorsCb(uint16_t handle,const ble_gatt_error *error,uint16_t,const ble_gatt_dsc *dsc,void *arg) {
    if(!valid(handle,arg)) return 0;
    if(error->status==BLE_HS_EDONE) {
        if(bootMode) subscribe();
        else if(workspace->inputs[cursor].reference) check(ble_gattc_read(connection,workspace->inputs[cursor].reference,referenceCb,token()));
        else { ++cursor;nextInput(); }
    } else if(error->status) fail(error->status);
    else if(dsc) {
        const auto uuid=ble_uuid_u16(&dsc->uuid.u);
        if(uuid==0x2902) workspace->inputs[cursor].cccd=dsc->handle;
        if(uuid==0x2908) workspace->inputs[cursor].reference=dsc->handle;
    }
    return 0;
}
void nextInput() {
    if(cursor==inputCount) {
        if(!subscribed || (!keyboardAvailable.load()&&!gamepadAvailable.load())) { fail(BLE_HS_ENOTSUP);return; }
        unsigned ids=0;
        for(unsigned i=0;i<inputCount;++i) {
            const auto &in=workspace->inputs[i];
            if(in.inputReport && in.id>=1 && in.id<=6)ids|=1u<<in.id;
        }
        iinePadProfile=gamepadAvailable.load() && !keyboardAvailable.load() && inputCount==6 && ids==0x7e &&
            gamepad::iineCReportMap(workspace->mapBytes.data(),mapLength);
        // A saved GAP name can already be generic before this connection starts.
        // Recover H navigation from the captured descriptor AND report topology.
        if(keyboardAvailable.load() && !gamepadAvailable.load() && !bootMode && inputCount==6 && ids==0x7e &&
           gamepad::iineHReportMap(workspace->mapBytes.data(),mapLength)) iineProfile=true;
        stage=Stage::Ready;isReady.store(true);BleHid::keyboardDiscoveryFinished(true,0);
        traceMap();
        ESP_LOGI(tag,"HID controller profile h=%d c=%d",int(iineProfile.load()),int(iinePadProfile.load()));
        ESP_LOGI(tag,"HID input ready, reports=%u keyboard=%d gamepad=%d protocol=%s",subscribed,int(keyboardAvailable.load()),int(gamepadAvailable.load()),bootMode?"boot":"report");return;
    }
    if(workspace->inputs[cursor].end<=workspace->inputs[cursor].value) { ++cursor;nextInput();return; }
    stage=Stage::References;
    check(ble_gattc_disc_all_dscs(connection,workspace->inputs[cursor].value,workspace->inputs[cursor].end,descriptorsCb,token()));
}
void prepareInputs() {
    inputCount=cursor=subscribed=0;
    for(unsigned i=0;i<charCount;++i) {
        const auto &c=workspace->chars[i];
        if(!(c.properties&BLE_GATT_CHR_PROP_NOTIFY) || (bootMode?c.value!=bootHandle:c.uuid!=0x2a4d)) continue;
        if(inputCount==workspace->inputs.size()) { fail(BLE_HS_ENOMEM);return; }
        auto &in=workspace->inputs[inputCount++];in={};in.value=c.value;
        in.end=i+1<charCount?workspace->chars[i+1].declaration-1:serviceEnd;
    }
    if(!inputCount) { fail(BLE_HS_ENOTSUP);return; }
    if(protocol) {
        // Protocol Mode is Write Without Response in HOGP.
        const uint8_t mode=bootMode?0:1;
        const int rc=ble_gattc_write_no_rsp_flat(connection,protocol,&mode,1);
        if(rc) { fail(rc);return; }
    } else if(bootMode) { fail(BLE_HS_ENOTSUP);return; }
    nextInput();
}
int mapCb(uint16_t handle,const ble_gatt_error *error,ble_gatt_attr *attr,void *arg) {
    if(!valid(handle,arg)) return 0;
    if(error->status==BLE_HS_EDONE) {
        keyboardMap=workspace->map.parse(workspace->mapBytes.data(),mapLength);
        padMap=workspace->gamepadMap.parse(workspace->mapBytes.data(),mapLength);
        ESP_LOGI(tag,"HID map bytes=%u keyboard=%d gamepad=%d boot=%u protocol=%u",unsigned(mapLength),int(keyboardMap),int(padMap),bootHandle,protocol);
        // A descriptor is public device metadata, not a stream of pressed keys.
        // Record it even when no notification arrives or pairing began in Settings.
        traceBytes("map",workspace->mapBytes.data(),mapLength,false);
        if(!keyboardMap&&!padMap) {
            if(!bootHandle || !protocol) { fail(BLE_HS_ENOTSUP);return 0; }
            bootMode=true;
        }
        prepareInputs();
    } else if(error->status) fail(error->status);
    else if(attr) {
        const size_t length=OS_MBUF_PKTLEN(attr->om);
        if(attr->offset!=mapLength || mapLength+length>workspace->mapBytes.size()) { fail(BLE_HS_EMSGSIZE);return BLE_HS_EMSGSIZE; }
        ble_hs_mbuf_to_flat(attr->om,workspace->mapBytes.data()+mapLength,length,nullptr);mapLength+=length;
    }
    return 0;
}
int characteristicsCb(uint16_t handle,const ble_gatt_error *error,const ble_gatt_chr *chr,void *arg) {
    if(!valid(handle,arg)) return 0;
    if(error->status==BLE_HS_EDONE) {
        if(mapHandle) {stage=Stage::ReportMap;check(ble_gattc_read_long(connection,mapHandle,0,mapCb,token()));}
        else if(bootHandle && protocol) { bootMode=true;prepareInputs(); }
        else fail(BLE_HS_ENOTSUP);
    } else if(error->status) fail(error->status);
    else if(chr) {
        if(charCount==workspace->chars.size()) { fail(BLE_HS_ENOMEM);return BLE_HS_ENOMEM; }
        const uint16_t uuid=ble_uuid_u16(&chr->uuid.u);
        workspace->chars[charCount++]={chr->def_handle,chr->val_handle,uuid,chr->properties};
        if(uuid==0x2a4b) mapHandle=chr->val_handle;
        if(uuid==0x2a4e) protocol=chr->val_handle;
        if(uuid==0x2a22) bootHandle=chr->val_handle;
    }
    return 0;
}
int serviceCb(uint16_t handle,const ble_gatt_error *error,const ble_gatt_svc *svc,void *arg) {
    if(!valid(handle,arg)) return 0;
    if(error->status==BLE_HS_EDONE) {
        if(!serviceStart) { fail(BLE_HS_ENOENT);return 0; }
        stage=Stage::Characteristics;
        check(ble_gattc_disc_all_chrs(connection,serviceStart,serviceEnd,characteristicsCb,token()));
    } else if(error->status) fail(error->status);
    else if(svc && !serviceStart) { serviceStart=svc->start_handle;serviceEnd=svc->end_handle; }
    return 0;
}
}
void BleKeyboardClient::reset() {
    ++generation;connection=invalid;isReady.store(false);keyboardAvailable=false;gamepadAvailable=false;
    iineProfile=false;iinePadProfile=false;
    KeyboardInput::reset();gamepad::reset();
    // Retain the reason after the GAP disconnect following a discovery failure.
    if(stage.load()!=Stage::Failed)stage=Stage::Idle;
}
void BleKeyboardClient::start(uint16_t handle) {
    reset();connection=handle;stage=Stage::Service;received=0;rejected=0;changes=0;lastError=0;
    // GAP 0x2a00 may later replace the advertised model name with a generic name.
    // Keep the input profile for this connection; never carry it to another peer.
    iineProfile=gamepad::iineKeyboard(BleHid::peer().name);
    if(diagnosticVisible.load()){mapTracePending=true;traceRemaining=64;traceOtherRemaining=8;++traceSession;}
    if(!workspace) {
        void *memory=heap_caps_malloc(sizeof(Workspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!memory) { fail(BLE_HS_ENOMEM);return; }
        workspace=new(memory) Workspace{};
    }
    serviceStart=serviceEnd=mapHandle=protocol=bootHandle=0;
    charCount=inputCount=cursor=subscribed=0;mapLength=0;bootMode=keyboardMap=padMap=false;
    const ble_uuid16_t uuid=BLE_UUID16_INIT(0x1812);
    check(ble_gattc_disc_svc_by_uuid(handle,&uuid.u,serviceCb,token()));
}
bool BleKeyboardClient::ready() { return isReady.load(); }
bool BleKeyboardClient::keyboardReady() {return ready()&&keyboardAvailable.load();}
bool BleKeyboardClient::gamepadReady() {return ready()&&gamepadAvailable.load();}
bool BleKeyboardClient::iineKeyboard() {
    return keyboardReady() && (iineProfile.load() || gamepad::iineKeyboard(BleHid::peer().name));
}
bool BleKeyboardClient::iineGamepad() {return gamepadReady() && iinePadProfile.load();}
BleKeyboardClient::Diagnostics BleKeyboardClient::diagnostics() {return {stage.load(),received.load(),rejected.load(),lastError.load(),changes.load()};}
void BleKeyboardClient::diagnosticView(bool visible) {
    diagnosticVisible=false;
    mapTracePending=visible;traceRemaining=visible?64:0;traceOtherRemaining=visible?8:0;++traceSession;
    diagnosticVisible=visible;
}
void BleKeyboardClient::notification(uint16_t handle,uint16_t attribute,const uint8_t *data,size_t length) {
    if(handle!=connection || !isReady.load()) return;
    // Raw test samples are restricted to the foreground FC Bluetooth page.
    // No serial key logging in gameplay, text entry, password or other apps.
    if(diagnosticVisible.load())traceMap();
    for(unsigned i=0;i<inputCount;++i) {
        auto &in=workspace->inputs[i];if(in.value!=attribute || !in.enabled) continue;
        traceSample(in,data,length);
        received.fetch_add(1);
        if(!in.seen){in.seen=true;ESP_LOGI(tag,"HID first input handle=%u id=%u bytes=%u mapped=%d",attribute,in.id,unsigned(length),int(mapped(in)));}
        if(!mapped(in)){++rejected;return;}
        hidinput::Keys decoded;
        if(bootMode) {
            if(length!=8) {++rejected;return;}
            for(unsigned b=0;b<8;++b) if(data[0]&(1u<<b)) decoded.set(0xe0+b);
            for(unsigned j=2;j<8;++j) { if(data[j]>=1 && data[j]<=3) return;if(data[j]>=4) decoded.set(data[j]); }
        } else if(keyboardMap&&workspace->map.has(in.id)&&!workspace->map.decode(in.id,data,length,decoded)) {++rejected;return;}
        bool changed=decoded.bits!=in.keys.bits;
        if(padMap&&workspace->gamepadMap.has(in.id)) {
            gamepad::State pad;
            // A malformed packet must not leave a previous direction held.
            if(!workspace->gamepadMap.decode(in.id,data,length,pad))++rejected;
            changed|=in.pad.packed()!=pad.packed();in.pad=pad;
            gamepad::State mergedPad;
            for(unsigned n=0;n<inputCount;++n){mergedPad.buttons|=workspace->inputs[n].pad.buttons;mergedPad.directions|=workspace->inputs[n].pad.directions;}
            gamepad::publish(mergedPad);
        }
        if(changed)++changes;
        in.keys=decoded;hidinput::Keys merged;
        for(unsigned n=0;n<inputCount;++n) for(unsigned byte=0;byte<merged.bits.size();++byte) merged.bits[byte]|=workspace->inputs[n].keys.bits[byte];
        if(keyboardAvailable.load())KeyboardInput::publish(merged);
        return;
    }
}
}
