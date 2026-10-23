// Production HID map + production asynchronous GATT discovery, synthetic peers.
#include "core/ble_keyboard_client.hpp"
#include "core/hid_input_report.hpp"
#include "core/hid_gamepad_report.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <new>
#include <cstdarg>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct ble_uuid_t{uint16_t value;};
struct ble_uuid16_t{ble_uuid_t u;};
#define BLE_UUID16_INIT(x) {{x}}
static uint16_t ble_uuid_u16(const ble_uuid_t *u){return u->value;}
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE=0xffff;
constexpr int BLE_HS_EDONE=14,BLE_HS_EBADDATA=2,BLE_HS_ENOTSUP=8,BLE_HS_ENOMEM=6,BLE_HS_EMSGSIZE=4,BLE_HS_ENOENT=5,BLE_ERR_REM_USER_CONN_TERM=19,BLE_GATT_CHR_PROP_NOTIFY=16;
struct Buffer{std::vector<uint8_t> bytes;};
#define OS_MBUF_PKTLEN(p) ((p)->bytes.size())
struct ble_gatt_error{int status;};
struct ble_gatt_attr{uint16_t offset;Buffer *om;};
struct ble_gatt_svc{uint16_t start_handle,end_handle;};
struct ble_gatt_chr{uint16_t def_handle,val_handle;ble_uuid16_t uuid;uint8_t properties;};
struct ble_gatt_dsc{ble_uuid16_t uuid;uint16_t handle;};
static int ble_hs_mbuf_to_flat(Buffer*b,void*p,size_t n,void*){CHECK(n<=b->bytes.size());std::memcpy(p,b->bytes.data(),n);return 0;}
namespace fake{int error=0,failed=0,success=0,terminated=0,keyboardReports=0;bool oom=false;sys::hidinput::Keys keys;unsigned inputTraces=0,mapTraces=0;char name[64]="IINE-keyboard";}
template<class... A> int ble_gattc_write_flat(A...){return fake::error;}
template<class... A> int ble_gattc_read(A...){return fake::error;}
template<class... A> int ble_gattc_disc_all_dscs(A...){return fake::error;}
template<class... A> int ble_gattc_write_no_rsp_flat(A...){return fake::error;}
template<class... A> int ble_gattc_read_long(A...){return fake::error;}
template<class... A> int ble_gattc_disc_all_chrs(A...){return fake::error;}
template<class... A> int ble_gattc_disc_svc_by_uuid(A...){return fake::error;}
static int ble_gap_terminate(uint16_t,int){++fake::terminated;return 0;}
static void logInfo(const char *,const char *format,...) {
 if(std::strstr(format,"FC HID sample"))++fake::inputTraces;
 if(std::strstr(format,"FC HID %s")){va_list args;va_start(args,format);const auto kind=va_arg(args,const char *);if(!std::strcmp(kind,"map"))++fake::mapTraces;va_end(args);}
}
#define ESP_LOGI(...) logInfo(__VA_ARGS__)
#define ESP_LOGW(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void *heap_caps_malloc(size_t n,int){return fake::oom?nullptr:std::malloc(n);}
namespace sys {
struct BleHid{struct Peer{const char *name;};static Peer peer(){return {fake::name};}static void keyboardDiscoveryFinished(bool success,int){if(success)++fake::success;else ++fake::failed;}};
struct KeyboardInput{
 static void reset(){fake::keys={};}
 static void publish(hidinput::Keys k){fake::keys=k;++fake::keyboardReports;}
};
}
#include "core/hid_iine_profile.hpp"
#include "ble_client.inc"
#include "iine_fixture.inc"
// Synthetic standards-based fixture: two report IDs, signed X/Y, 1-based hat,
// and ten buttons. This is deliberately not labelled a captured L1162 report.
static const std::vector<uint8_t> padFixture={
 0x05,1,0x09,5,0xa1,1,0x85,1,
 0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,2,
 0x09,0x39,0x15,1,0x25,8,0x75,4,0x95,1,0x81,0x42,
 0x75,4,0x95,1,0x81,1,
 0x85,7,0x05,9,0x19,1,0x29,10,0x15,0,0x25,1,0x75,1,0x95,10,0x81,2,
 0x75,6,0x95,1,0x81,1,0xc0};
static const std::vector<uint8_t> keyMap={
 0x05,1,0x09,6,0xa1,1,0x85,3,0x05,7,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};
// Unsigned 16-bit sticks, 0-based hat and non-byte-aligned fields. Synthetic.
static const std::vector<uint8_t> wideMap={
 0x05,1,0x09,5,0xa1,1,0x85,2,0x75,3,0x95,1,0x81,1,
 0x09,0x30,0x09,0x31,0x15,0,0x26,0xff,0xff,0x75,16,0x95,2,0x81,2,
 0x09,0x39,0x25,7,0x75,4,0x95,1,0x81,0x42,0xa4,
 0x05,9,0x19,1,0x29,10,0x25,1,0x75,1,0x95,10,0x81,2,0xb4,
 0x75,7,0x95,1,0x81,1,0xc0};
static void bits(uint8_t *p,unsigned offset,unsigned n,uint32_t value){
 for(unsigned i=0;i<n;++i){p[(offset+i)/8]&=uint8_t(~(1u<<((offset+i)%8)));p[(offset+i)/8]|=((value>>i)&1u)<<((offset+i)%8);}
}
static ble_gatt_error ok{0},done{BLE_HS_EDONE};
static void discover(const std::vector<uint8_t>&map,unsigned reports=2,bool boot=false){
 using namespace sys;
 BleKeyboardClient::start(12);CHECK(!BleKeyboardClient::ready());
 ble_gatt_svc svc{1,90};serviceCb(12,&ok,&svc,token());serviceCb(12,&done,nullptr,token());
 const ble_gatt_chr mc{2,3,{{0x2a4b}},0};characteristicsCb(12,&ok,&mc,token());
 for(unsigned i=0;i<reports;++i){const ble_gatt_chr c{uint16_t(5+i*8),uint16_t(6+i*8),{{0x2a4d}},BLE_GATT_CHR_PROP_NOTIFY};characteristicsCb(12,&ok,&c,token());}
 if(boot){ble_gatt_chr b{40,41,{{0x2a22}},BLE_GATT_CHR_PROP_NOTIFY};characteristicsCb(12,&ok,&b,token());ble_gatt_chr p{45,46,{{0x2a4e}},0};characteristicsCb(12,&ok,&p,token());}
 characteristicsCb(12,&done,nullptr,token());
 const size_t half=map.size()/2;
 Buffer b{{map.begin(),map.begin()+half}};ble_gatt_attr a{0,&b};mapCb(12,&ok,&a,token());
 b.bytes.assign(map.begin()+half,map.end());a.offset=half;mapCb(12,&ok,&a,token());
 mapCb(12,&done,nullptr,token());
}
static void subscribe(uint8_t id,int error=0){
 using namespace sys;
 auto &input=workspace->inputs[cursor];const auto v=input.value;
 ble_gatt_dsc c{{{0x2902}},uint16_t(v+1)},r{{{0x2908}},uint16_t(v+2)};
 descriptorsCb(12,&ok,0,&c,token());descriptorsCb(12,&ok,0,&r,token());descriptorsCb(12,&done,0,nullptr,token());
 if(!bootMode){Buffer b{{id,1}};ble_gatt_attr a{0,&b};referenceCb(12,&ok,&a,token());}
 ble_gatt_error result{error};subscribedCb(12,&result,nullptr,token());
}
int main(){
 using namespace sys;
 hidinput::GamepadReportMap map;CHECK(map.parse(padFixture.data(),padFixture.size()));CHECK(map.has(1)&&map.has(7)&&!map.has(2));
 constexpr uint8_t hats[]={0,0x10,0x90,0x80,0xa0,0x20,0x60,0x40,0x50,0,0,0,0,0,0,0};
 for(unsigned h=0;h<16;++h){uint8_t data[]={0,0,uint8_t(h)};gamepad::State p;CHECK(map.decode(1,data,3,p)&&p.directions==hats[h]&&p.buttons==0);}
 for(int axis=-127;axis<=127;++axis){uint8_t data[]={uint8_t(axis),0,0};gamepad::State p;CHECK(map.decode(1,data,3,p));CHECK(p.directions==(axis<-63?0x40:axis>63?0x80:0));}
 for(unsigned buttons=0;buttons<1024;++buttons){uint8_t data[]={uint8_t(buttons),uint8_t(buttons>>8)};gamepad::State p;CHECK(map.decode(7,data,2,p)&&p.buttons==buttons);CHECK(gamepad::fcPad(p)==((buttons&3)|((buttons>>6)&12)));}
 for(size_t n=0;n<padFixture.size();++n){hidinput::GamepadReportMap m;(void)m.parse(padFixture.data(),n);}
 for(size_t n=0;n<3;++n){uint8_t data[3]{};gamepad::State p{77,0};CHECK(!map.decode(1,data,n,p)&&p.buttons==77);}
 CHECK(!map.parse(nullptr,0));CHECK(!map.parse(keyMap.data(),keyMap.size()));
 CHECK(map.parse(wideMap.data(),wideMap.size()));
 constexpr uint8_t wideHats[]={0x10,0x90,0x80,0xa0,0x20,0x60,0x40,0x50,0,0,0,0,0,0,0,0};
 for(unsigned h=0;h<16;++h){uint8_t data[7]{};bits(data,3,16,32768);bits(data,19,16,32768);bits(data,35,4,h);bits(data,39,10,0x303);gamepad::State s;
  CHECK(map.decode(2,data,7,s)&&s.directions==wideHats[h]&&s.buttons==0x303);}
 for(unsigned x:{0u,16383u,16384u,32768u,49151u,49152u,65535u}){
  uint8_t data[7]{};bits(data,3,16,x);bits(data,19,16,32768);bits(data,35,4,15);gamepad::State s;
  CHECK(map.decode(2,data,7,s)&&s.directions==(x<16384?0x40:x>49151?0x80:0));
 }
 for(size_t n=0;n<wideMap.size();++n)CHECK(!map.parse(wideMap.data(),n));
 for(auto bad:std::vector<std::vector<uint8_t>>{{0xb4},{0xc0},{0xa4,0xa4,0xa4,0xa4,0xa4},{0x85,0},{0x77,0,0,0},{0x05}})CHECK(!map.parse(bad.data(),bad.size()));
 std::vector<uint8_t> huge(2049,0);CHECK(!map.parse(huge.data(),huge.size()));
 auto mixed=keyMap;mixed.insert(mixed.end(),padFixture.begin(),padFixture.end());
 CHECK(map.parse(mixed.data(),mixed.size())&&map.has(1)&&map.has(7)&&!map.has(3));
 // Ignore a mouse application, support neutral/diagonal/short taps and fences.
 auto mouse=padFixture;mouse[3]=2;CHECK(!map.parse(mouse.data(),mouse.size()));
 gamepad::reset();gamepad::publish({1,0x90});gamepad::publish({});CHECK(gamepad::snapshot().packed()==0);CHECK(gamepad::fcPad(gamepad::takePresses())==0x91);CHECK(!gamepad::takePresses().packed());
 gamepad::Gate gate;gate.clear({1,0x10});CHECK(!gate.filter({1,0x10}).packed());CHECK(!gate.filter({}).packed());CHECK(gate.filter({1,0x10}).packed());
 CHECK(gamepad::fcPad({3,0xf0})==3);CHECK(gamepad::iineKeyboard("IINE-keyboard")&&gamepad::iineKeyboard("IINE-1001")&&!gamepad::iineKeyboard("IINE-mouse"));
 fake::oom=true;BleKeyboardClient::start(12);CHECK(fake::failed==1&&!BleKeyboardClient::ready());fake::oom=false;
 discover(mixed,3);subscribe(3);subscribe(1);subscribe(7);
 CHECK(BleKeyboardClient::keyboardReady()&&BleKeyboardClient::gamepadReady());
 uint8_t mixedKeys[]={4,0,0,0,0,0},mixedDirection[]={0,0,3},mixedButtons[]={2,0};
 BleKeyboardClient::notification(12,6,mixedKeys,6);BleKeyboardClient::notification(12,14,mixedDirection,3);BleKeyboardClient::notification(12,22,mixedButtons,2);
 CHECK(fake::keys.down(4)&&gamepad::fcPad(gamepad::snapshot())==0x82);BleKeyboardClient::reset();
 for(unsigned cycle=0;cycle<30;++cycle){
  discover(padFixture);subscribe(1);CHECK(!BleKeyboardClient::ready());subscribe(7);CHECK(BleKeyboardClient::gamepadReady()&&!BleKeyboardClient::keyboardReady());
  auto reports=fake::keyboardReports;uint8_t direction[]={0,0,2},button[]={3,3};
  BleKeyboardClient::notification(12,6,direction,3);BleKeyboardClient::notification(12,14,button,2);
  CHECK(gamepad::fcPad(gamepad::snapshot())==0x9f&&fake::keyboardReports==reports);
  auto diagnostics=BleKeyboardClient::diagnostics();CHECK(diagnostics.stage==BleKeyboardClient::Stage::Ready&&diagnostics.received==2&&diagnostics.rejected==0);
  BleKeyboardClient::notification(13,6,direction,3);CHECK(gamepad::snapshot().buttons==0x303);
  BleKeyboardClient::notification(12,6,direction,1);CHECK(gamepad::snapshot().directions==0&&gamepad::snapshot().buttons==0x303);
  CHECK(BleKeyboardClient::diagnostics().received==3&&BleKeyboardClient::diagnostics().rejected==1);
  auto stale=token();BleKeyboardClient::reset();CHECK(!gamepad::snapshot().packed()&&!BleKeyboardClient::ready());
  subscribedCb(12,&ok,nullptr,stale);BleKeyboardClient::notification(12,14,button,2);CHECK(!BleKeyboardClient::ready()&&!gamepad::snapshot().packed());
  discover(keyMap,1);subscribe(3);CHECK(BleKeyboardClient::keyboardReady()&&!BleKeyboardClient::gamepadReady());
  uint8_t keys[]={4,5,0x52,0,0,0};BleKeyboardClient::notification(12,6,keys,6);CHECK(fake::keys.down(4)&&fake::keys.down(5)&&fake::keys.down(0x52));BleKeyboardClient::reset();
  discover(padFixture);subscribe(1,77);CHECK(!BleKeyboardClient::ready()&&!gamepad::snapshot().packed());
  CHECK(BleKeyboardClient::diagnostics().stage==BleKeyboardClient::Stage::Failed&&BleKeyboardClient::diagnostics().error==77);
  BleKeyboardClient::reset();CHECK(BleKeyboardClient::diagnostics().error==77);
 }
 // Broken map falls back only when actual Boot Keyboard + Protocol exist.
 discover({0xfe},0,true);subscribe(0);CHECK(BleKeyboardClient::keyboardReady()&&!BleKeyboardClient::gamepadReady());
 uint8_t boot[]={0,0,4,0,0,0,0,0};BleKeyboardClient::notification(12,41,boot,8);CHECK(fake::keys.down(4));BleKeyboardClient::reset();
 discover({0xfe},1);CHECK(!BleKeyboardClient::ready());
 fake::error=77;BleKeyboardClient::start(12);CHECK(!BleKeyboardClient::ready());fake::error=0;
 // Preserve the selected controller profile through GAP display-name updates.
 std::strcpy(fake::name,"IINE-keyboard");discover(keyMap,1);subscribe(3);CHECK(BleKeyboardClient::iineKeyboard());
 std::strcpy(fake::name,"HID Keyboard");CHECK(BleKeyboardClient::iineKeyboard());
 const uint8_t sample[]={4,0,0,0,0,0};BleKeyboardClient::notification(12,6,sample,6);CHECK(!fake::inputTraces);
 BleKeyboardClient::diagnosticView(true);
 for(unsigned i=0;i<100;++i)BleKeyboardClient::notification(12,6,sample,6);
 CHECK(fake::inputTraces==1&&fake::mapTraces>0);
 for(unsigned i=0;i<200;++i){uint8_t changing[]={uint8_t(i%2?4:5),0,0,0,0,0};BleKeyboardClient::notification(12,6,changing,6);}
 CHECK(fake::inputTraces==64);const auto maps=fake::mapTraces;
 BleKeyboardClient::diagnosticView(false);BleKeyboardClient::notification(12,6,sample,6);
 CHECK(fake::inputTraces==64&&fake::mapTraces==maps);
 BleKeyboardClient::reset();CHECK(!BleKeyboardClient::iineKeyboard());
 discover(keyMap,1);subscribe(3);CHECK(!BleKeyboardClient::iineKeyboard());
 BleKeyboardClient::notification(12,6,sample,6);CHECK(fake::inputTraces==64);BleKeyboardClient::reset();
 // A multi-function peripheral's unmapped input channels are subscribed, but
 // their notifications never release keys or become FC controls.
 discover(keyMap,2);subscribe(1);CHECK(!BleKeyboardClient::ready());subscribe(3);
 CHECK(BleKeyboardClient::ready()&&subscribed==2&&workspace->inputs[0].enabled);
 BleKeyboardClient::notification(12,14,sample,6);CHECK(fake::keys.down(4));
 const auto published=fake::keyboardReports;BleKeyboardClient::notification(12,6,sample,6);
 CHECK(fake::keys.down(4)&&fake::keyboardReports==published&&BleKeyboardClient::diagnostics().rejected==1);
 BleKeyboardClient::reset();
 // An optional channel failure cannot block a supported input channel.
 discover(keyMap,2);subscribe(1,77);subscribe(3);CHECK(BleKeyboardClient::ready()&&subscribed==1);
 BleKeyboardClient::reset();
 discover(keyMap,1);subscribe(1);CHECK(!BleKeyboardClient::ready());BleKeyboardClient::reset();
 // Real captured L1162 descriptor and neutral frame; pressed frames below are
 // synthetic probes, not claims about the physical A/B button assignments.
 discover(iineMap,6);for(uint8_t id=1;id<=6;++id)subscribe(id);
 CHECK(BleKeyboardClient::keyboardReady()&&subscribed==6&&workspace->map.has(2));
 CHECK(!std::strcmp(fake::name,"HID Keyboard")&&BleKeyboardClient::iineKeyboard());
 uint8_t realNeutral[8]{};for(unsigned i=0;i<1000;++i)BleKeyboardClient::notification(12,14,realNeutral,8);
 CHECK(BleKeyboardClient::diagnostics().received==1000&&BleKeyboardClient::diagnostics().changes==0);
 for(uint8_t usage:{uint8_t(4),uint8_t(5),uint8_t(0x28),uint8_t(0x29),uint8_t(0x52)}){
  uint8_t probe[]={0,0,usage,0,0,0,0,0};BleKeyboardClient::notification(12,14,probe,8);CHECK(fake::keys.down(usage));
  BleKeyboardClient::notification(12,14,realNeutral,8);CHECK(!fake::keys.down(usage));
 }
 CHECK(BleKeyboardClient::diagnostics().changes==10);
 // Byte-exact B/release/A/release captured on the user's H-mode controller.
 // GATT handles are translated to the synthetic service used by this replay.
 for(const auto &captured:iineSamples){
  BleKeyboardClient::notification(12,14,captured.bytes.data(),captured.bytes.size());
  uint8_t pad=0;for(const auto &binding:gamepad::iineBindings)if(fake::keys.down(binding.usage))pad|=binding.pad;
  CHECK(pad==captured.pad);
 }
 CHECK(BleKeyboardClient::diagnostics().changes==14);BleKeyboardClient::reset();
 // The keyboard descriptor alone is insufficient: don't reinterpret Space on
 // an ordinary keyboard, a changed map, or a different report topology.
 discover(iineMap,1);subscribe(2);CHECK(!BleKeyboardClient::iineKeyboard());BleKeyboardClient::reset();
 discover(iineMap,6);for(uint8_t id:{1,2,3,4,5,5})subscribe(id);CHECK(!BleKeyboardClient::iineKeyboard());BleKeyboardClient::reset();
 auto differentHMap=iineMap;differentHMap[7]=3;
 discover(differentHMap,6);for(uint8_t id=1;id<=6;++id)subscribe(id);CHECK(!BleKeyboardClient::iineKeyboard());BleKeyboardClient::reset();
 // C-mode capture: exact descriptor + six input references identifies the
 // mini controller layout. Never swap every device named Xbox Controller.
 std::strcpy(fake::name,"Xbox Wireless Controller");
 discover(iineCMap,6);for(uint8_t id=1;id<=6;++id)subscribe(id);
 CHECK(BleKeyboardClient::iineGamepad()&&!BleKeyboardClient::iineKeyboard());
 for(const auto &captured:iineCSamples){
  BleKeyboardClient::notification(12,6,captured.bytes.data(),captured.bytes.size());
  CHECK(gamepad::fcPad(gamepad::snapshot(),BleKeyboardClient::iineGamepad())==captured.pad);
 }
 CHECK(BleKeyboardClient::diagnostics().changes==12&&BleKeyboardClient::diagnostics().rejected==0);
 BleKeyboardClient::reset();CHECK(!BleKeyboardClient::iineGamepad());
 discover(iineCMap,1);subscribe(1);CHECK(BleKeyboardClient::gamepadReady()&&!BleKeyboardClient::iineGamepad());BleKeyboardClient::reset();
 discover(iineCMap,6);for(uint8_t id:{1,2,3,4,5,5})subscribe(id);CHECK(!BleKeyboardClient::iineGamepad());BleKeyboardClient::reset();
 auto differentCMap=iineCMap;differentCMap[3]=4;
 discover(differentCMap,6);for(uint8_t id=1;id<=6;++id)subscribe(id);
 CHECK(BleKeyboardClient::gamepadReady()&&!BleKeyboardClient::iineGamepad());BleKeyboardClient::reset();
 std::printf("%u checks; HID ranges/IDs, 30 GATT attach/reset cycles, keyboard coexistence, CCCD failure, stale callbacks, short reports and boot fallback passed; no physical controller used\n",checks);
}
