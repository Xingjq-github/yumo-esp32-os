#include "core/settings_store.hpp"
#include "core/mp3_model.hpp"
#include "esp_err.h"
#include <algorithm>
#include <mutex>
#include <new>
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;static bool failAllocation=false;static int failAfter=-1;
void *operator new(size_t n){if(failAllocation||failAfter==0){failAllocation=false;failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}void operator delete(void *p,size_t)noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGW(...)
using nvs_handle_t=int;constexpr int NVS_READONLY=0,NVS_READWRITE=1;
static int opens=0,closes=0,commits=0,commitError=0;static int64_t now=10000000;
static std::vector<std::string> written;
static auto driftBlob=sys::clock_drift::encode({-123456,3}),pendingDrift=driftBlob;
static int64_t esp_timer_get_time(){return now;}
static int nvs_open(const char*,int,int *h){*h=1;++opens;pendingDrift=driftBlob;return 0;}
static void nvs_close(int){++closes;}
static int nvs_commit(int){++commits;if(!commitError)driftBlob=pendingDrift;return commitError;}
static int nvs_get_str(int,const char*,char*,size_t*){return -1;}
static int nvs_get_i32(int,const char*,int32_t*){return -1;}
static int nvs_get_i64(int,const char*,int64_t*){return -1;}
static int nvs_get_u32(int,const char*,uint32_t*){return -1;}
static int nvs_get_blob(int,const char *key,void *data,size_t *size){
 if(std::strcmp(key,"rtc_drift_v1")||*size<driftBlob.size())return -1;
 std::memcpy(data,driftBlob.data(),driftBlob.size());*size=driftBlob.size();return 0;
}
static int nvs_set_i32(int,const char*k,int32_t){written.emplace_back(k);return 0;}
static int nvs_set_i64(int,const char*k,int64_t){written.emplace_back(k);return 0;}
static int nvs_set_u32(int,const char*k,uint32_t){written.emplace_back(k);return 0;}
static int nvs_set_str(int,const char*k,const char*){written.emplace_back(k);return 0;}
static int nvs_set_blob(int,const char*k,const void *data,size_t size){written.emplace_back(k);
 if(!std::strcmp(k,"rtc_drift_v1")){CHECK(size==pendingDrift.size());std::memcpy(pendingDrift.data(),data,size);}return 0;}
struct FakeTimeval{int64_t tv_sec,tv_usec;};static int fakeSetTime(FakeTimeval*,void*){return 0;}
#include "settings.inc"
int main(){using sys::Settings;written.reserve(1000);Settings::load();
 CHECK(Settings::clockDrift().ratePpb==-123456&&Settings::clockDrift().samples==3);opens=closes=0;
 CHECK(Settings::bluetoothOn()&&Settings::wifiOn()&&!Settings::motionSensorOn());
 Settings::setBluetoothOn(false);Settings::setWifiOn(true);Settings::setMotionSensorOn(true);CHECK(Settings::flush(true)&&commits==0);
 Settings::setWifiCredentials(std::string(30,'s'),std::string(60,'p'));Settings::rememberWifi(std::string(30,'s'),std::string(60,'p'));
 failAllocation=true;CHECK(!Settings::flush(true)&&commits==0&&opens==0);
 CHECK(Settings::flush(true)&&commits==1&&opens==closes);
 const auto oldSsid=Settings::wifiSsid(),oldPassword=Settings::wifiPassword();
 const std::string nextSsid(31,'n'),nextPassword(61,'q');
 bool threw=false;failAfter=1;
 try{Settings::setWifiCredentials(nextSsid,nextPassword);}catch(const std::bad_alloc&){threw=true;}
 CHECK(threw&&Settings::wifiSsid()==oldSsid&&Settings::wifiPassword()==oldPassword);
 const auto oldLan=Settings::lanSsid(),oldLanPassword=Settings::lanPassword();
 threw=false;failAfter=1;
 try{Settings::setLanCredentials(nextSsid,nextPassword);}catch(const std::bad_alloc&){threw=true;}
 CHECK(threw&&Settings::lanSsid()==oldLan&&Settings::lanPassword()==oldLanPassword);
 threw=false;failAfter=1;
 try{Settings::rememberWifi(oldSsid,nextPassword);}catch(const std::bad_alloc&){threw=true;}
 CHECK(threw&&Settings::wifiPasswordFor(oldSsid)==oldPassword);
 threw=false;failAfter=2;
 try{Settings::rememberWifi(oldSsid,nextPassword);}catch(const std::bad_alloc&){threw=true;}
 CHECK(threw&&Settings::wifiPasswordFor(oldSsid)==oldPassword);
 CHECK(Settings::flush(true)&&commits==1);
 written.clear();Settings::saveClock(1800000000);failAllocation=true;CHECK(Settings::flush(true));CHECK(failAllocation);failAllocation=false;
 CHECK(written.size()==1&&written.front()=="clock");
 Settings::setVolume(51);commitError=-1;CHECK(!Settings::flush(true));commitError=0;CHECK(Settings::flush(true));
 written.clear();const auto before=commits;Settings::saveClockDrift({-4000000,4});
 CHECK(commits==before&&written.empty()); // save only queues RAM; worker owns NVS
 commitError=-1;CHECK(!Settings::flush(true));CHECK(sys::clock_drift::decode(driftBlob).ratePpb==-123456);
 Settings::saveClockDrift({-3900000,5});commitError=0;CHECK(Settings::flush(true));
 CHECK(sys::clock_drift::decode(driftBlob).ratePpb==-3900000&&sys::clock_drift::decode(driftBlob).samples==5);
 CHECK(written.size()==2&&written[0]=="rtc_drift_v1"&&written[1]=="rtc_drift_v1");
 const auto stable=commits;Settings::saveClockDrift({-3900000,5});CHECK(Settings::flush(true)&&commits==stable);
 Settings::saveClockDrift({50000000,5});CHECK(Settings::clockDrift().ratePpb==-3900000&&Settings::flush(true)&&commits==stable);
 CHECK(opens==closes);for(auto &key:written)CHECK(key!="bt"&&key!="wifi"&&key!="motion");
 std::printf("%u settings checks; volatile switches, zero-allocation clock flush, OOM and commit retry\n",checks);
}
