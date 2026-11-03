#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>
#include "core/ble_hid.hpp"
#include "core/hid_mouse_queue.hpp"
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=257,ESP_ERR_INVALID_ARG=258;
constexpr int ESP_ERR_INVALID_STATE=259,ESP_ERR_INVALID_SIZE=260,ESP_ERR_NOT_FOUND=261;
constexpr int ESP_ERR_NOT_SUPPORTED=262,ESP_ERR_TIMEOUT=263;
constexpr int BLE_GAP_ROLE_SLAVE=1,BLE_ERR_REM_USER_CONN_TERM=19;
template<typename... T> void ignoreLog(T...){ }
#define ESP_LOGI(...) ignoreLog(__VA_ARGS__)
#define ESP_LOGW(...) ignoreLog(__VA_ARGS__)
unsigned checks=0,scenarios=0;
#define CHECK(v) do{++checks;if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::abort();}}while(0)
struct ble_npl_event{};
struct ble_npl_callout {void(*fn)(ble_npl_event*)=nullptr;int64_t due=0;bool active=false;};
struct ble_gap_conn_desc{uint16_t conn_itvl=12,conn_latency=0;uint8_t role=BLE_GAP_ROLE_SLAVE;};
struct ble_gap_upd_params{uint16_t itvl_min=0,itvl_max=0,latency=0,supervision_timeout=0,min_ce_len=0,max_ce_len=0;};
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
namespace fake {
int64_t now=1000000;bool remote=true,screen=true,sleep=false,inHost=false;
int locks=0,initError=0,scheduleFailures=0,linkError=0,updateError=0;
int fallbackError=0;unsigned attempts=0,terminations=0,updates=0;
ble_gap_conn_desc desc{};ble_gap_upd_params updated{};
std::deque<int> errors;
std::function<void()> duringSend;
std::function<void()> beforeCritical;
struct Sent{uintptr_t link;std::array<uint8_t,4> bytes;int64_t at;};
std::vector<Sent> sent;
}
void portENTER_CRITICAL(portMUX_TYPE*){
    if(fake::beforeCritical){auto call=std::move(fake::beforeCritical);fake::beforeCritical={};call();}
    ++fake::locks;
}
void portEXIT_CRITICAL(portMUX_TYPE*){CHECK(fake::locks>0);--fake::locks;}
int64_t esp_timer_get_time(){return fake::now;}
void* nimble_port_get_dflt_eventq(){return nullptr;}
uint32_t ble_npl_time_ms_to_ticks32(uint32_t n){return n;}
int ble_npl_callout_init(ble_npl_callout *t,void*,void(*fn)(ble_npl_event*),void*){
    if(fake::initError)return fake::initError;
    t->fn=fn;return 0;
}
int ble_npl_callout_reset(ble_npl_callout *t,uint32_t delay){
    CHECK(fake::locks==0);CHECK(t->fn);CHECK(delay>0);
    if(fake::scheduleFailures){--fake::scheduleFailures;return -1;}
    t->active=true;t->due=fake::now+int64_t(delay)*1000;return 0;
}
void ble_npl_callout_stop(ble_npl_callout *t){t->active=false;}
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc *out){CHECK(fake::locks==0);*out=fake::desc;return fake::linkError;}
int ble_gap_update_params(uint16_t,const ble_gap_upd_params *p){CHECK(fake::inHost);CHECK(fake::locks==0);++fake::updates;fake::updated=*p;return fake::updateError;}
int ble_gap_terminate(uint16_t,int){CHECK(fake::inHost);CHECK(fake::locks==0);++fake::terminations;return 0;}
namespace sys {
struct Power{static bool isScreenOn(){return fake::screen;}static bool sleepPending(){return fake::sleep;}};
constexpr uint8_t kMouseReportId=2;
constexpr const char *kTag="mouse";
std::atomic<bool> s_hostRunning{true};
std::atomic<uintptr_t> s_linkGeneration{1};
std::atomic<uint16_t> s_connHandle{42};
bool BleHid::remoteConnected(){return fake::remote;}
esp_err_t sendReport(uint8_t id,uint8_t *data,size_t length){
    CHECK(fake::inHost);CHECK(fake::locks==0);CHECK(id==2);CHECK(length==4);
    const auto link=s_linkGeneration.load();
    int result=fake::fallbackError;
    if(!fake::errors.empty()){result=fake::errors.front();fake::errors.pop_front();}
    ++fake::attempts;
    if(fake::duringSend){auto call=std::move(fake::duringSend);fake::duringSend={};call();}
    if(!result)fake::sent.push_back({link,{data[0],data[1],data[2],data[3]},fake::now});
    return result;
}
#include "mouse_scheduler.inc"
#include "mouse_api.inc"
}
using sys::BleHid;
void advance(unsigned ms){
    const auto target=fake::now+int64_t(ms)*1000;
    unsigned loops=0;
    while(sys::s_mouse.timer.active && sys::s_mouse.timer.due<=target){
        CHECK(++loops<10000);fake::now=sys::s_mouse.timer.due;
        sys::s_mouse.timer.active=false;fake::inHost=true;
        sys::s_mouse.timer.fn(nullptr);fake::inHost=false;
    }
    fake::now=target;
}
void reset(){
    ++scenarios;fake::now=1000000;fake::remote=fake::screen=true;fake::sleep=false;
    fake::inHost=false;fake::locks=0;fake::initError=fake::scheduleFailures=fake::linkError=fake::updateError=0;
    fake::fallbackError=0;fake::attempts=fake::terminations=fake::updates=0;fake::desc={};fake::updated={};
    fake::errors.clear();fake::duringSend={};fake::beforeCritical={};fake::sent.clear();
    ++sys::s_linkGeneration;sys::s_hostRunning=true;sys::resetMouseOutput();
}
int sum(size_t axis){int n=0;for(const auto &r:fake::sent)n+=int8_t(r.bytes[axis]);return n;}
void checkPace(unsigned ms){for(size_t i=1;i<fake::sent.size();++i)CHECK(fake::sent[i].at-fake::sent[i-1].at>=int64_t(ms)*1000);}

int main(){
    reset();CHECK(BleHid::mouseIdle());CHECK(BleHid::sendMouse(10,20)==ESP_OK);
    CHECK(fake::sent.empty());CHECK(!BleHid::mouseIdle());advance(100);
    CHECK(fake::sent.size()==1);CHECK(sum(1)==10&&sum(2)==20);CHECK(BleHid::mouseIdle());

    reset();for(int i=0;i<10;++i)CHECK(BleHid::sendMouse(20,-15,2)==ESP_OK);
    CHECK(sys::s_mouse.queue.size()==1);advance(500);
    CHECK(sum(1)==200&&sum(2)==-150&&sum(3)==20);checkPace(15);CHECK(fake::sent.size()==2);

    reset();for(unsigned i=0;i<100;++i){CHECK(BleHid::sendMouse(1,-1)==ESP_OK);advance(5);}
    advance(100);CHECK(sum(1)==100&&sum(2)==-100);checkPace(15);
    CHECK(fake::sent.size()<=35&&BleHid::mouseIdle());

    reset();for(int i=0;i<10000;++i)CHECK(BleHid::sendMouse(127,-127,127)==ESP_OK);
    CHECK(sys::s_mouse.queue.size()==1);advance(1000);
    CHECK(sum(1)==4096&&sum(2)==-4096&&sum(3)==4096);CHECK(fake::sent.size()==33);CHECK(BleHid::mouseIdle());

    reset();CHECK(BleHid::sendMouse(1792,1344)==ESP_OK);advance(1000);
    CHECK(sum(1)==1792&&sum(2)==1344);CHECK(BleHid::mouseIdle());
    reset();CHECK(BleHid::sendMouse(INT32_MAX,INT32_MIN,INT32_MAX)==ESP_OK);
    CHECK(BleHid::sendMouse(INT32_MAX,INT32_MIN,INT32_MAX)==ESP_OK);advance(1000);
    CHECK(sum(1)==4096&&sum(2)==-4096&&sum(3)==4096);

    reset();const auto oldLink=BleHid::mouseLinkGeneration();++sys::s_linkGeneration;
    CHECK(BleHid::sendMouse(1,1,0,0,oldLink)==ESP_ERR_INVALID_STATE);
    CHECK(BleHid::clickMouse(1,oldLink)==ESP_ERR_INVALID_STATE);CHECK(fake::sent.empty());
    CHECK(BleHid::mouseLinkGeneration()!=oldLink);
    for(bool click:{false,true}){
        reset();const auto expected=BleHid::mouseLinkGeneration();
        fake::beforeCritical=[](){++sys::s_linkGeneration;};
        CHECK((click?BleHid::clickMouse(1,expected):BleHid::sendMouse(1,1,0,0,expected))==ESP_ERR_INVALID_STATE);
        advance(100);CHECK(fake::sent.empty());
    }

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);CHECK(fake::sent.empty());advance(1);
    CHECK(fake::sent.size()==1&&fake::sent[0].bytes[0]==1);advance(19);CHECK(fake::sent.size()==1);
    advance(1);CHECK(fake::sent.size()==2&&fake::sent[1].bytes[0]==0);CHECK(BleHid::mouseIdle());

    reset();fake::desc.conn_itvl=24;CHECK(BleHid::sendMouse(127,0)==ESP_OK);
    CHECK(BleHid::sendMouse(127,0)==ESP_OK);CHECK(BleHid::clickMouse(2)==ESP_OK);advance(500);
    checkPace(30);CHECK(sum(1)==254);CHECK(fake::sent.back().bytes[0]==0);

    reset();fake::errors={ESP_ERR_NO_MEM,ESP_ERR_TIMEOUT,ESP_OK};CHECK(BleHid::sendMouse(37,-42)==ESP_OK);
    advance(200);CHECK(fake::attempts==3);CHECK(sum(1)==37&&sum(2)==-42);
    CHECK(!fake::terminations&&BleHid::takeMouseError()==ESP_OK&&BleHid::mouseIdle());

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);advance(1);
    fake::fallbackError=ESP_ERR_NO_MEM;advance(650);CHECK(fake::terminations==0);CHECK(!BleHid::mouseIdle());
    CHECK(BleHid::takeMouseError()==ESP_ERR_NO_MEM);advance(100);CHECK(BleHid::takeMouseError()==ESP_OK);
    fake::fallbackError=0;advance(100);CHECK(fake::sent.back().bytes[0]==0&&BleHid::mouseIdle());

    reset();fake::fallbackError=ESP_ERR_NO_MEM;CHECK(BleHid::sendMouse(60,0)==ESP_OK);advance(1000);
    CHECK(BleHid::mouseIdle()&&fake::sent.empty()&&!fake::terminations);CHECK(BleHid::takeMouseError()==ESP_ERR_NO_MEM);
    fake::fallbackError=0;CHECK(BleHid::sendMouse(3,0)==ESP_OK);advance(50);CHECK(sum(1)==3);

    for(const int error:{ESP_ERR_NOT_FOUND,ESP_ERR_INVALID_STATE,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_SIZE,ESP_ERR_NOT_SUPPORTED}){
        reset();fake::fallbackError=error;CHECK(BleHid::clickMouse(1)==ESP_OK);advance(50);
        CHECK(BleHid::mouseIdle());CHECK(fake::attempts==1);CHECK(!fake::terminations);CHECK(BleHid::takeMouseError()==error);
    }

    reset();CHECK(BleHid::sendMouse(0,0,0,1)==ESP_OK);advance(1);CHECK(!BleHid::mouseIdle());
    fake::fallbackError=ESP_ERR_NOT_FOUND;BleHid::cancelMouse();advance(20);CHECK(fake::terminations==1);
    fake::remote=false;advance(100);CHECK(BleHid::mouseIdle());

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);advance(1);fake::fallbackError=ESP_ERR_TIMEOUT;
    advance(3950);CHECK(!fake::terminations);advance(200);CHECK(fake::terminations==1);
    CHECK(BleHid::sendMouse(1,1)==ESP_ERR_INVALID_STATE);
    fake::fallbackError=0;advance(100);CHECK(BleHid::mouseIdle()&&fake::sent.back().bytes[0]==0);

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);BleHid::cancelMouse();advance(500);
    CHECK(fake::sent.empty()&&BleHid::mouseIdle());

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);fake::duringSend=[](){BleHid::cancelMouse();};advance(100);
    CHECK(fake::sent.size()==2);CHECK(fake::sent[0].bytes[0]==1&&fake::sent[1].bytes[0]==0);CHECK(BleHid::mouseIdle());

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);fake::duringSend=[](){
        ++sys::s_linkGeneration;sys::resetMouseOutput();CHECK(BleHid::clickMouse(2)==ESP_OK);
    };advance(100);
    CHECK(fake::sent.size()==3);CHECK(fake::sent[0].link!=fake::sent[1].link);
    CHECK(fake::sent[1].bytes[0]==2&&fake::sent[2].bytes[0]==0);

    reset();CHECK(BleHid::sendMouse(5,0,0,1)==ESP_OK);advance(1);const auto count=fake::attempts;
    advance(1000);sys::recoverMouseSchedule();CHECK(fake::attempts==count&&!BleHid::mouseIdle());
    BleHid::cancelMouse();advance(100);CHECK(BleHid::mouseIdle()&&fake::sent.back().bytes[0]==0);

    for(int mode=0;mode<2;++mode){reset();CHECK(BleHid::clickMouse(1)==ESP_OK);advance(1);
        if(mode){fake::screen=false;BleHid::cancelMouse();}else fake::sleep=true;
        advance(100);CHECK(BleHid::mouseIdle()&&fake::sent.back().bytes[0]==0);
        if(!mode)CHECK(BleHid::sendMouse(1,1)==ESP_ERR_INVALID_STATE);
        else{CHECK(BleHid::clickMouse(1)==ESP_OK);advance(100);CHECK(BleHid::mouseIdle());}
    }
    reset();fake::remote=false;CHECK(BleHid::sendMouse(1,1)==ESP_ERR_INVALID_STATE);CHECK(BleHid::clickMouse(1)==ESP_ERR_INVALID_STATE);
    fake::remote=true;sys::s_hostRunning=false;CHECK(BleHid::clickMouse(1)==ESP_ERR_INVALID_STATE);
    sys::s_hostRunning=true;CHECK(BleHid::sendMouse(0,0,0,8)==ESP_ERR_INVALID_ARG);CHECK(BleHid::clickMouse(0)==ESP_ERR_INVALID_ARG);

    reset();CHECK(BleHid::sendMouse(0,0,0,1)==ESP_OK);advance(1);
    unsigned accepted=0;for(unsigned i=0;i<20;++i){if(BleHid::sendMouse(1,0,0,uint8_t((i%2)+1))==ESP_OK)++accepted;}
    CHECK(accepted<20);CHECK(sys::s_mouse.queue.size()<=15);
    CHECK(BleHid::sendMouse(0,0,0,0)==ESP_OK);BleHid::cancelMouse();advance(500);
    CHECK(fake::sent.back().bytes[0]==0&&BleHid::mouseIdle());CHECK(sys::s_mouse.queue.size()==0);

    reset();fake::scheduleFailures=1;CHECK(BleHid::clickMouse(1)==ESP_FAIL);advance(100);
    CHECK(fake::sent.empty()&&BleHid::mouseIdle());CHECK(BleHid::takeMouseError()==ESP_FAIL);

    reset();CHECK(BleHid::sendMouse(0,0,0,1)==ESP_OK);advance(1);fake::scheduleFailures=1;
    BleHid::cancelMouse();CHECK(!BleHid::mouseIdle());sys::recoverMouseSchedule();advance(100);
    CHECK(BleHid::mouseIdle()&&fake::sent.back().bytes[0]==0);

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);fake::duringSend=[](){fake::scheduleFailures=1;};advance(100);
    CHECK(fake::sent.size()==2&&fake::sent.back().bytes[0]==0);CHECK(BleHid::mouseIdle());CHECK(!fake::terminations);

    reset();CHECK(BleHid::clickMouse(1)==ESP_OK);fake::duringSend=[](){fake::scheduleFailures=1;fake::fallbackError=ESP_FAIL;};advance(100);
    CHECK(fake::terminations==1);CHECK(BleHid::mouseIdle());

    reset();fake::desc.conn_itvl=30;fake::desc.conn_latency=4;
    CHECK(BleHid::sendMouse(1,1)==ESP_OK);advance(100);CHECK(fake::updates==0);
    advance(5000);CHECK(BleHid::sendMouse(1,1)==ESP_OK);advance(50);CHECK(fake::updates==1);
    CHECK(fake::updated.itvl_min==9&&fake::updated.itvl_max==12&&fake::updated.latency==0&&fake::updated.supervision_timeout==600);
    CHECK(BleHid::sendMouse(1,1)==ESP_OK);advance(50);CHECK(fake::updates==1&&!fake::terminations);

    reset();fake::desc.conn_itvl=30;fake::updateError=7;advance(5000);
    CHECK(BleHid::sendMouse(1,1)==ESP_OK);advance(50);CHECK(fake::updates==1&&!fake::terminations);

    for(unsigned seed=1;seed<=40;++seed){
        reset();unsigned rng=seed,terminations=0;
        for(unsigned i=0;i<1000;++i){
            rng=rng*1664525u+1013904223u;
            switch((rng>>16)%8){
            case 0:BleHid::cancelMouse();break;
            case 1:(void)BleHid::clickMouse(uint8_t(1u<<(rng%3)));break;
            case 2:fake::errors.push_back(ESP_ERR_NO_MEM);break;
            default:(void)BleHid::sendMouse(int8_t(rng),int8_t(rng>>8),int8_t((rng>>24)%5),uint8_t((rng>>20)&1));break;
            }
            advance((rng>>27)%18);CHECK(sys::s_mouse.queue.size()<=sys::hid::MouseQueue::Capacity);
            CHECK(fake::locks==0);
            if(fake::terminations!=terminations){
                CHECK(sys::s_mouse.failedSince!=0);CHECK(fake::now-sys::s_mouse.failedSince>=4000000);
                CHECK(fake::terminations==terminations+1);terminations=fake::terminations;
                ++sys::s_linkGeneration;sys::resetMouseOutput();fake::errors.clear();
            }
        }
        fake::errors.clear();fake::fallbackError=0;BleHid::cancelMouse();advance(2000);
        CHECK(BleHid::mouseIdle());CHECK(fake::sent.empty()||fake::sent.back().bytes[0]==0);
    }
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u}\n",scenarios,checks);
}
