// Run the production Connectivity implementation with deterministic IDF faults.
// This validates ownership/state transitions, not real RF or heap capacity.
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

static unsigned checks=0, scenarios=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"scenario %u line %d: %s\n",scenarios,__LINE__,#x); std::exit(1); } } while(0)
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_ARG=0x102,
    ESP_ERR_INVALID_STATE=0x103, ESP_ERR_NOT_FINISHED=0x10c, ESP_ERR_TIMEOUT=0x107,
    ESP_ERR_WIFI_NOT_STARTED=0x3002, ESP_ERR_WIFI_NOT_STOPPED=0x3003,
    ESP_ERR_WIFI_NOT_CONNECT=0x300f, ESP_ERR_WIFI_STATE=0x3006;
constexpr int WIFI_REASON_AUTH_FAIL=202, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=15,
    WIFI_REASON_HANDSHAKE_TIMEOUT=204, WIFI_REASON_NO_AP_FOUND=201, WIFI_REASON_ASSOC_LEAVE=8,
    WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY=4;
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_SPIRAM=4;
constexpr int WIFI_AUTH_OPEN=0,WIFI_AUTH_WPA_PSK=1,WIFI_IF_STA=0,WIFI_MODE_STA=1,WIFI_STORAGE_RAM=0;
constexpr int WIFI_PS_MIN_MODEM=1,WIFI_PS_NONE=0,ESP_EVENT_ANY_ID=-1,pdTRUE=1;
constexpr int WIFI_FAST_SCAN=0,WIFI_ALL_CHANNEL_SCAN=1,WIFI_CONNECT_AP_BY_SIGNAL=0;
constexpr unsigned WIFI_PASSIVE_SCAN_DEFAULT_TIME=360,WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME=30;
using esp_event_base_t=int;
constexpr int WIFI_EVENT=1,IP_EVENT=2,WIFI_EVENT_SCAN_DONE=1,WIFI_EVENT_STA_START=2,
    WIFI_EVENT_STA_STOP=3,WIFI_EVENT_STA_DISCONNECTED=4,WIFI_EVENT_STA_CONNECTED=5,
    IP_EVENT_STA_GOT_IP=1,IP_EVENT_STA_LOST_IP=2;
struct wifi_ap_record_t { uint8_t ssid[33]{}; int8_t rssi{}; uint8_t primary{}; };
struct wifi_init_config_t { int unused{}; };
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t { struct { uint8_t ssid[32]{},password[64]{},channel{}; bool bssid_set{};
    int scan_method{},sort_method{}; uint8_t failure_retry_cnt{};
    struct { int authmode{}; } threshold; struct { bool capable{}; } pmf_cfg; } sta; };
struct wifi_scan_default_params_t {
    struct { struct { uint32_t min{},max{}; } active; uint32_t passive{}; } scan_time;
    uint8_t home_chan_dwell_time{};
};
struct wifi_scan_config_t { uint8_t *ssid{}; bool show_hidden{};
    struct { struct { uint32_t min{},max{}; } active; } scan_time; };
struct wifi_event_sta_scan_done_t { uint32_t status{}; };
struct wifi_event_sta_disconnected_t { uint8_t ssid[32]{}; uint8_t ssid_len{}; int reason{}; };
struct wifi_event_sta_connected_t { uint8_t ssid[32]{}; uint8_t ssid_len{}; };
struct esp_netif_config_t { int unused{}; };
#define ESP_NETIF_DEFAULT_WIFI_STA() esp_netif_config_t{}
struct esp_netif_t { bool attached{}; };
struct esp_ip4_addr_t { uint32_t addr{}; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip,netmask,gw; };
constexpr int ESP_IPADDR_TYPE_V4=0,ESP_NETIF_DNS_MAIN=0,ESP_NETIF_DNS_BACKUP=1;
struct esp_netif_dns_info_t {struct {int type=0;struct {esp_ip4_addr_t ip4;} u_addr;} ip;};
struct Handler { esp_event_base_t base; void (*fn)(void *,esp_event_base_t,int32_t,void *); };
using esp_event_handler_instance_t=Handler*;
struct TestQueue { size_t size{}; std::deque<std::vector<uint8_t>> items; };
using StaticQueue_t=TestQueue;
using QueueHandle_t=TestQueue*;

namespace fake {
std::string fail;
unsigned allocations=0,driverInits=0,driverDeinits=0,starts=0,stops=0,connects=0,scans=0,remembered=0;
unsigned cleanupFailures=0,unregisterFailures=0,logs=0;
std::vector<std::string> logLines;
unsigned scanBusy=0,scanAttempts=0;
unsigned recordsRead=0, recordsFailAfter=~0u;
bool dropDirected=false;
std::string scanFilter;
uint8_t connectChannel=0;
wifi_scan_default_params_t scanParameters{};
wifi_config_t connectConfig{};
unsigned scanParameterCalls=0;
bool driver=false,started=false,stopPending=false,scan=false,defaultHandlers=false,wifiOn=false;
bool associated=false,hasIp=false,sleepAllowed=true;
bool linkActive=false,disconnectRequested=false;
bool bleConnected=true; // retained through every Wi-Fi operation; no BLE API is stubbed
int64_t time=1000000;
std::map<void *,size_t> blocks;
std::vector<Handler *> handlers;
esp_netif_t *netif=nullptr;
std::vector<wifi_ap_record_t> records;
std::deque<wifi_ap_record_t> results;
bool fault(const char *name) { return fail==name; }
void emit(int base,int id,void *data=nullptr) {
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP)stopPending=false;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        linkActive=false;disconnectRequested=false;associated=false;hasIp=false;
    }
    for(auto *h:handlers) if(h->base==base)h->fn(nullptr,base,id,data);
}
void scanDone(int status=0) {
    scan=false; results.clear(); recordsRead=0;
    for(const auto &ap:records) if(scanFilter.empty() || (!dropDirected && scanFilter==reinterpret_cast<const char *>(ap.ssid))) results.push_back(ap);
    wifi_event_sta_scan_done_t event{uint32_t(status)};emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&event);
}
void disconnectDone(int reason=WIFI_REASON_ASSOC_LEAVE) {
    wifi_event_sta_disconnected_t event{};event.reason=reason;
    std::strcpy(reinterpret_cast<char*>(event.ssid),"test-ap");event.ssid_len=7;
    emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&event);
}
void log(const char *level,const char *tag,const char *format,...) {
    char message[1024];va_list args;va_start(args,format);
    const int length=std::vsnprintf(message,sizeof(message),format,args);va_end(args);
    CHECK(length>=0 && size_t(length)<sizeof(message));
    ++logs;logLines.emplace_back(std::string(level)+" "+tag+" "+message);
}
}
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    const auto name="scan"+std::to_string(++fake::allocations);
    if(fake::fail==name)return nullptr;
    auto *p=std::calloc(n,size);CHECK(p);fake::blocks[p]=n*size;return p;
}
static void heap_caps_free(void *p) { if(p){CHECK(fake::blocks.erase(p)==1);std::free(p);} }
static unsigned heap_caps_get_free_size(int) { return 12345; }
static unsigned heap_caps_get_largest_free_block(int) { return 6789; }
static const char *esp_err_to_name(esp_err_t) { return "injected"; }
#define ESP_LOGE(...) fake::log("E",__VA_ARGS__)
#define ESP_LOGW(...) fake::log("W",__VA_ARGS__)
#define ESP_LOGI(...) fake::log("I",__VA_ARGS__)
static int64_t esp_timer_get_time() { return fake::time; }
static QueueHandle_t xQueueCreateStatic(unsigned,size_t size,uint8_t*,StaticQueue_t *q) { q->size=size;return q; }
static int xQueueSend(QueueHandle_t q,const void *p,int) {
    CHECK(q);if(q->items.size()>=16)return 0;
    auto *s=static_cast<const uint8_t*>(p);q->items.emplace_back(s,s+q->size);return pdTRUE;
}
static int xQueueReceive(QueueHandle_t q,void *p,int) {
    CHECK(q);if(q->items.empty())return 0;
    std::memcpy(p,q->items.front().data(),q->size);q->items.pop_front();return pdTRUE;
}
static void xQueueReset(QueueHandle_t q) { q->items.clear(); }
static esp_err_t esp_wifi_init(const wifi_init_config_t*) {
    CHECK(!fake::driver);++fake::driverInits;
    if(fake::fault("driver"))return ESP_ERR_NO_MEM;
    fake::driver=true;return ESP_OK;
}
static esp_err_t esp_wifi_deinit() {
    ++fake::driverDeinits;CHECK(fake::driver);CHECK(!fake::stopPending);
    if(fake::started)return ESP_ERR_WIFI_NOT_STOPPED;
    if(fake::cleanupFailures){--fake::cleanupFailures;return ESP_FAIL;}
    fake::driver=false;fake::scan=false;fake::scanParameters={};return ESP_OK;
}
static esp_netif_t *esp_netif_new(const esp_netif_config_t*) {
    CHECK(!fake::netif);if(fake::fault("netif"))return nullptr;
    return fake::netif=new esp_netif_t{};
}
static esp_err_t esp_netif_attach_wifi_station(esp_netif_t *n) {
    if(fake::fault("attach"))return ESP_ERR_NO_MEM;n->attached=true;return ESP_OK;
}
static esp_err_t esp_wifi_set_default_wifi_sta_handlers() {
    if(fake::fault("defaults"))return ESP_ERR_NO_MEM;fake::defaultHandlers=true;return ESP_OK;
}
static void esp_netif_destroy_default_wifi(esp_netif_t *n) {
    CHECK(!fake::driver);CHECK(fake::handlers.empty());CHECK(n==fake::netif);
    delete n;fake::netif=nullptr;fake::defaultHandlers=false;
}
static esp_err_t esp_wifi_set_storage(int) { return fake::fault("storage")?ESP_ERR_NO_MEM:ESP_OK; }
static esp_err_t esp_wifi_set_mode(int) { return fake::fault("mode")?ESP_ERR_NO_MEM:ESP_OK; }
static esp_err_t esp_event_handler_instance_register(int base,int,void (*fn)(void*,int,int32_t,void*),void*,Handler **out) {
    if(fake::fault(base==WIFI_EVENT?"wifi-handler":"ip-handler"))return ESP_ERR_NO_MEM;
    *out=new Handler{base,fn};fake::handlers.push_back(*out);return ESP_OK;
}
static esp_err_t esp_event_handler_instance_unregister(int,int,Handler *h) {
    CHECK(!fake::driver);
    if(fake::unregisterFailures){--fake::unregisterFailures;return ESP_FAIL;}
    auto it=std::find(fake::handlers.begin(),fake::handlers.end(),h);CHECK(it!=fake::handlers.end());
    fake::handlers.erase(it);delete h;return ESP_OK;
}
static esp_err_t esp_wifi_start() {
    CHECK(fake::driver);CHECK(!fake::stopPending);++fake::starts;
    if(fake::fault("start"))return ESP_ERR_NO_MEM;
    if(fake::started)return ESP_ERR_WIFI_NOT_STOPPED;
    fake::started=true;return ESP_OK; // test delivers START later
}
static esp_err_t esp_wifi_stop() {
    CHECK(fake::driver);++fake::stops;
    if(fake::fault("stop"))return ESP_FAIL;
    if(!fake::started)return ESP_ERR_WIFI_NOT_STARTED;
    fake::started=false;fake::stopPending=true;fake::associated=false;fake::hasIp=false;
    fake::linkActive=false;fake::disconnectRequested=false;return ESP_OK;
}
static esp_err_t esp_wifi_disconnect() {
    if(fake::fault("disconnect"))return ESP_FAIL;
    if(!fake::linkActive)return ESP_ERR_WIFI_NOT_CONNECT;
    fake::disconnectRequested=true;return ESP_OK; // terminal callback is deliberately delayed
}
static esp_err_t esp_wifi_set_ps(int) { CHECK(fake::driver);return ESP_OK; }
static esp_err_t esp_wifi_set_max_tx_power(int) { CHECK(fake::driver);return ESP_OK; }
static esp_err_t esp_wifi_clear_ap_list() { CHECK(fake::driver);fake::results.clear();return ESP_OK; }
static esp_err_t esp_wifi_scan_stop() { CHECK(fake::driver);return ESP_OK; }
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config,bool) {
    CHECK(fake::driver && fake::started);CHECK(!fake::scan);++fake::scanAttempts;
    if(fake::linkActive || fake::disconnectRequested)return ESP_ERR_WIFI_STATE;
    if(fake::scanBusy){--fake::scanBusy;return ESP_ERR_WIFI_STATE;}
    fake::scanFilter=config->ssid?reinterpret_cast<const char *>(config->ssid):"";
    fake::scan=true;++fake::scans;return ESP_OK;
}
static esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { *n=uint16_t(fake::results.size());return ESP_OK; }
static esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *out) {
    if(fake::recordsRead==fake::recordsFailAfter)return ESP_FAIL;
    if(fake::results.empty())return ESP_FAIL;
    *out=fake::results.front();fake::results.pop_front();++fake::recordsRead;return ESP_OK;
}
static esp_err_t esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *params) {
    CHECK(fake::started && !fake::scan && !fake::linkActive && !fake::disconnectRequested);
    ++fake::scanParameterCalls;
    if(fake::fault("scan-defaults"))return ESP_FAIL;
    fake::scanParameters=*params;return ESP_OK;
}
static esp_err_t esp_wifi_set_config(int,const wifi_config_t *config) {
    CHECK(fake::started);CHECK(!config->sta.bssid_set);fake::connectChannel=config->sta.channel;
    fake::connectConfig=*config;return ESP_OK;
}
static esp_err_t esp_wifi_connect() {
    CHECK(fake::started && !fake::scan && !fake::disconnectRequested);
    CHECK(fake::scanParameters.scan_time.active.min==120 && fake::scanParameters.scan_time.active.max==300);
    CHECK(fake::scanParameters.scan_time.passive==360 && fake::scanParameters.home_chan_dwell_time==30);
    const auto &config=fake::connectConfig.sta;
    CHECK(config.scan_method==WIFI_FAST_SCAN || config.scan_method==WIFI_ALL_CHANNEL_SCAN);
    if(config.scan_method==WIFI_FAST_SCAN) {
        CHECK(config.channel>=1 && config.channel<=13);
        CHECK(config.failure_retry_cnt==0);
    } else CHECK(config.failure_retry_cnt==1);
    CHECK(fake::connectConfig.sta.sort_method==WIFI_CONNECT_AP_BY_SIGNAL);
    ++fake::connects;fake::linkActive=true;return ESP_OK;
}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {
    if(!fake::associated)return ESP_ERR_WIFI_NOT_CONNECT;
    std::strcpy(reinterpret_cast<char*>(out->ssid),"test-ap");out->rssi=-40;return ESP_OK;
}
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char*) { return fake::netif; }
static esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t *out) {
    out->ip.addr=fake::hasIp?1:0;out->netmask.addr=0xffffff00;out->gw.addr=fake::hasIp?2:0;return ESP_OK;
}
static esp_err_t esp_netif_get_dns_info(esp_netif_t*,int,esp_netif_dns_info_t *out){out->ip.u_addr.ip4.addr=3;return ESP_OK;}
static esp_err_t esp_netif_set_default_netif(esp_netif_t *n){CHECK(n==fake::netif);return ESP_OK;}
static void esp_ip4addr_ntoa(const esp_ip4_addr_t*,char *p,size_t n) { std::snprintf(p,n,"192.0.2.1"); }
namespace sys {
enum class EventType {WifiDisconnected,WifiConnecting,WifiConnected,WifiFailed};
struct Event { EventType type;int error; };
struct EventBus { static void publish(Event){} };
struct Settings {
    static void setWifiOn(bool on){fake::wifiOn=on;}
    static void setWifiCredentials(const std::string&,const std::string&){}
    static void rememberWifi(const std::string&,const std::string&){++fake::remembered;}
};
struct Power { static bool automaticSleepAllowed(){return fake::sleepAllowed;} };
}
#include "core/connectivity.hpp"
#include "connectivity.inc"

using sys::Connectivity;
using sys::WifiState;
using sys::WifiConnectPhase;
static void clean() {
    CHECK(!fake::driver && !fake::started && !fake::stopPending && !fake::scan);
    CHECK(!fake::netif && fake::handlers.empty() && fake::blocks.empty());
    CHECK(!fake::defaultHandlers && !fake::wifiOn && !Connectivity::wifiEnabled());
    CHECK(!sys::initialized && !sys::driverInitialized && !sys::wifiHandler && !sys::ipHandler);
    CHECK(!sys::networks && !sys::viewNetworks && sys::viewCount==0 && sys::networkCount==0);
    CHECK(!sys::connectionActive && !sys::ignoreDisconnect && !sys::disconnectDeadline);
    CHECK(!sys::probeRetryAt && !sys::probeBusyDeadline && !sys::probeRecovered);
    CHECK(!sys::broadcastProbe && sys::networksSeenAt==-1 && sys::targetSeenAt==-1 && sys::targetChannel==0);
    CHECK(fake::results.empty());
    CHECK(!Connectivity::snapshot().transitioning && !Connectivity::snapshot().scanPending);
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::None);
    CHECK(fake::bleConnected);
}
static void fresh() {
    clean();fake::fail.clear();fake::allocations=0;fake::records.clear();fake::scanBusy=0;++scenarios;
    fake::dropDirected=false;fake::recordsFailAfter=~0u;fake::logLines.clear();
    CHECK(!Connectivity::hotspotReserved());
}
static void start() {
    CHECK(Connectivity::setWifiEnabled(true)==ESP_OK);
    CHECK(Connectivity::snapshot().transitioning);
    CHECK(Connectivity::startScan()==ESP_ERR_INVALID_STATE);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();
    CHECK(!Connectivity::snapshot().transitioning && Connectivity::wifiEnabled());
}
static void stop() {
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    if(fake::stopPending){CHECK(fake::driver);fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();}
    clean();
}
static void selectAndAssociate() {
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::scan);
    wifi_ap_record_t record{};std::strcpy(reinterpret_cast<char*>(record.ssid),"test-ap");record.rssi=-40;record.primary=6;
    fake::records={record};fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Connecting);
}
static wifi_ap_record_t makeAp(const std::string &name,int rssi=-40,uint8_t channel=6) {
    CHECK(name.size()<=32);
    wifi_ap_record_t ap{};std::memcpy(ap.ssid,name.data(),name.size());ap.rssi=rssi;ap.primary=channel;return ap;
}
static void associated(const char *ssid="test-ap") {
    fake::associated=true;
    wifi_event_sta_connected_t event{};
    event.ssid_len=uint8_t(std::strlen(ssid));std::memcpy(event.ssid,ssid,event.ssid_len);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,&event);
}
static bool hasLog(const std::string &text) {
    return std::any_of(fake::logLines.begin(),fake::logLines.end(),[&](const auto &line){return line.find(text)!=std::string::npos;});
}
int main() {
    const char *faults[]={"scan1","scan2","driver","netif","attach","defaults","storage","mode","wifi-handler","ip-handler","start"};
    for(auto *fault:faults) {
        fresh();fake::fail=fault;
        CHECK(Connectivity::setWifiEnabled(true)==ESP_ERR_NO_MEM);
        CHECK(Connectivity::snapshot().state==WifiState::Failed);
        CHECK(Connectivity::snapshot().error==ESP_ERR_NO_MEM);clean();
        fake::fail.clear();start();stop(); // no reset/reboot needed
    }
    // Partial rollback cannot discard resources while deinit fails.
    fresh();fake::fail="mode";fake::cleanupFailures=1;
    CHECK(Connectivity::setWifiEnabled(true)==ESP_ERR_NO_MEM);
    CHECK(fake::driver && sys::driverInitialized && fake::netif && !fake::blocks.empty());
    CHECK(Connectivity::snapshot().error==ESP_ERR_NO_MEM);
    fake::fail.clear();start();stop();
    // Failed start + failed deinit must finish teardown before re-initializing.
    fresh();fake::fail="start";fake::cleanupFailures=1;
    CHECK(Connectivity::setWifiEnabled(true)==ESP_ERR_NO_MEM);
    CHECK(fake::driver && !sys::initialized && sys::driverInitialized);
    const auto initBeforeRetry=fake::driverInits;
    fake::fail.clear();start();CHECK(fake::driverInits==initBeforeRetry+1);stop();
    // Unregister failure similarly retains the instance/netif for retry.
    fresh();fake::fail="ip-handler";fake::unregisterFailures=1;
    CHECK(Connectivity::setWifiEnabled(true)==ESP_ERR_NO_MEM);
    CHECK(!fake::driver && !sys::initialized && sys::wifiHandler && fake::netif);
    fake::fail.clear();CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);clean();start();stop();
    // Closing must wait for STOP, including an outstanding scan and stale IP.
    fresh();start();CHECK(Connectivity::startScan()==ESP_OK);
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);CHECK(fake::driver);
    fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);fake::scanDone();
    Connectivity::poll();CHECK(fake::driver);CHECK(Connectivity::snapshot().state==WifiState::Off);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();clean();
    // STOP is delivered even if ordinary disconnect/IP notifications overflow.
    fresh();start();wifi_event_sta_disconnected_t disconnected{};disconnected.reason=WIFI_REASON_ASSOC_LEAVE;
    for(unsigned i=0;i<40;++i)fake::emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnected);
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();clean();
    // Hundreds of ordinary switches must return all owned allocations.
    for(unsigned i=0;i<200;++i) {fresh();start();stop();}
    // Off->On before STOP reuses resources, then waits for the new START.
    for(unsigned i=0;i<50;++i) {
        fresh();start();const auto inits=fake::driverInits,deinits=fake::driverDeinits;
        CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
        CHECK(Connectivity::setWifiEnabled(true)==ESP_OK);Connectivity::poll();
        CHECK(fake::stopPending && fake::driverInits==inits && fake::driverDeinits==deinits);
        fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
        CHECK(Connectivity::snapshot().transitioning && fake::started);
        fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();
        CHECK(!Connectivity::snapshot().transitioning);stop();
    }
    // A failed deferred restart rolls back and reports the initiating error.
    fresh();start();CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    CHECK(Connectivity::setWifiEnabled(true)==ESP_OK);fake::fail="start";
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
    CHECK(Connectivity::snapshot().error==ESP_ERR_NO_MEM);clean();fake::fail.clear();start();stop();
    // One failed deinit at ordinary STOP is not retried every poll.
    fresh();start();fake::cleanupFailures=1;
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
    CHECK(fake::driver && Connectivity::snapshot().state==WifiState::Failed);
    const auto deinitAttempts=fake::driverDeinits;
    for(unsigned i=0;i<30;++i)Connectivity::poll();
    CHECK(fake::driverDeinits==deinitAttempts);
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);clean();start();stop();
    // Missing STOP stays fenced and never frees a driver whose completion is unknown.
    fresh();start();CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    fake::time+=5000000;Connectivity::poll();
    CHECK(Connectivity::snapshot().error==ESP_ERR_TIMEOUT && fake::driver);
    CHECK(Connectivity::snapshot().scanPending);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();clean();
    // START can arrive after an immediate Off; it cannot keep the radio alive.
    fresh();CHECK(Connectivity::setWifiEnabled(true)==ESP_OK);
    CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();CHECK(fake::driver);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();clean();
    // Explicit init alone can be disabled without waiting for a nonexistent STOP.
    fresh();CHECK(Connectivity::init()==ESP_OK);CHECK(Connectivity::setWifiEnabled(false)==ESP_OK);clean();
    // A pending scan is fenced, then the enabled STA is borrowed without stop.
    fresh();start();CHECK(Connectivity::startScan()==ESP_OK);
    const auto revision=Connectivity::snapshot().revision;
    CHECK(Connectivity::acquireForHotspot()==ESP_ERR_NOT_FINISHED);
    fake::scanDone();Connectivity::poll();
    const auto beforeStops=fake::stops,beforeDeinits=fake::driverDeinits;
    CHECK(Connectivity::acquireForHotspot()==ESP_OK && Connectivity::hotspotUsesStation());
    CHECK(Connectivity::setWifiEnabled(true)==Connectivity::HotspotBusy);
    CHECK(Connectivity::setWifiEnabled(false)==Connectivity::HotspotBusy);
    CHECK(!Connectivity::hotspotUplink().online);
    CHECK(fake::stops==beforeStops&&fake::driverDeinits==beforeDeinits&&fake::started);
    CHECK(Connectivity::snapshot().revision>=revision);Connectivity::releaseFromHotspot();stop();
    // Disabled STA yields ownership for AP-only operation and stays disabled.
    fresh();CHECK(Connectivity::acquireForHotspot()==ESP_OK);CHECK(!Connectivity::hotspotUsesStation());
    CHECK(!Connectivity::hotspotUplink().online);Connectivity::releaseFromHotspot();clean();
    // Saved/entered credentials still need a presence scan; late IP cannot connect.
    fresh();start();const auto connects=fake::connects,remembered=fake::remembered;
    fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Disconnected);
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::connects==connects && fake::scan);
    wifi_ap_record_t record{};std::strcpy(reinterpret_cast<char*>(record.ssid),"test-ap");record.rssi=-40;record.primary=6;
    fake::records.push_back(record);fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==connects+1 && Connectivity::snapshot().state==WifiState::Connecting);
    CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN && fake::connectChannel==6);
    fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Connecting && fake::remembered==remembered);
    fake::associated=true;fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Connected && fake::remembered==remembered+1);
    wifi_ap_record_t result{};CHECK(Connectivity::scanResults(&result,1)==1);
    stop();CHECK(Connectivity::scanResults(&result,1)==0);
    // Busy driver is not an association failure: several deferrals must keep
    // the explicit target and the complete connection retry budget.
    fresh();start();fake::scanBusy=5;
    const auto beforeBusy=fake::connects;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    for(unsigned i=0;i<5;++i) {
        Connectivity::poll();
        CHECK(Connectivity::snapshot().state==WifiState::Searching);
        CHECK(Connectivity::snapshot().retry==0 && fake::connects==beforeBusy);
        fake::time+=250000;
    }
    Connectivity::poll();CHECK(fake::scan);
    fake::records={record};fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==beforeBusy+1);stop();
    // A timeout followed by a manual retry cannot overwrite the outstanding
    // intentional disconnect, or start a scan before that event is consumed.
    fresh();start();selectAndAssociate();fake::time+=21000000;Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Failed && fake::disconnectRequested);
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    const auto beforeLeave=fake::scanAttempts;
    Connectivity::poll();CHECK(fake::scanAttempts==beforeLeave);
    fake::disconnectDone();Connectivity::poll();CHECK(fake::scan);
    CHECK(Connectivity::snapshot().retry==0);stop();
    // An actual disconnect API failure cannot be silently turned into a scan.
    fresh();start();fake::fail="disconnect";
    const auto beforeDisconnectFailure=fake::scanAttempts;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_FAIL);
    Connectivity::poll();CHECK(fake::scanAttempts==beforeDisconnectFailure);
    fake::fail.clear();stop();
    // A genuinely stuck driver is restarted once, then bounded retries stop.
    // Neither hundreds of fast polls nor the recovery may exhaust resources.
    fresh();start();fake::scanBusy=10000;
    const auto beforeStuck=fake::starts,connectsBeforeStuck=fake::connects;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    for(unsigned i=0;i<180;++i) {
        Connectivity::poll();
        if(fake::stopPending) {
            fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
            fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();
        }
        fake::time+=250000;
    }
    CHECK(fake::starts==beforeStuck+1 && fake::connects==connectsBeforeStuck);
    CHECK(Connectivity::snapshot().state==WifiState::Failed);
    CHECK(Connectivity::snapshot().error==ESP_ERR_WIFI_STATE);
    CHECK(!Connectivity::snapshot().retrying && !sys::connectQueued);
    stop();
    // Losing an intentional disconnect callback uses STOP/START instead of
    // racing the next saved-network probe against the old association.
    fresh();start();selectAndAssociate();
    const auto scansBeforeLost=fake::scans;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    fake::time+=2100000;Connectivity::poll();CHECK(fake::stopPending);
    CHECK(fake::scans==scansBeforeLost && !sys::retryAt);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
    CHECK(fake::scans==scansBeforeLost);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();CHECK(fake::scan);
    fake::scanDone();Connectivity::poll();CHECK(Connectivity::snapshot().state==WifiState::Connecting);
    fake::associated=true;fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Connected);stop();
    // The expected leave after timeout must not consume/re-time a retry.
    fresh();start();selectAndAssociate();fake::time+=21000000;Connectivity::poll();
    const auto retryDeadline=sys::retryAt;
    CHECK(Connectivity::snapshot().retry==1 && retryDeadline>fake::time);
    fake::time+=250000;fake::disconnectDone();Connectivity::poll();
    CHECK(Connectivity::snapshot().retry==1 && sys::retryAt==retryDeadline);
    fake::disconnectDone();Connectivity::poll();
    CHECK(Connectivity::snapshot().retry==1 && sys::retryAt==retryDeadline);stop();
    // Explicit Off cancels even a deferred (not yet started) presence probe.
    fresh();start();fake::scanBusy=10;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(sys::probeRetryAt);stop();fake::time+=10000000;Connectivity::poll();clean();
    // Missing leave events during repeated connection timeouts cannot bypass
    // backoff or resurrect an exhausted connection when the driver restarts.
    fresh();start();selectAndAssociate();
    const auto firstTimeoutConnect=fake::connects;
    for(unsigned attempt=0;attempt<4;++attempt) {
        CHECK(Connectivity::snapshot().state==WifiState::Connecting);
        fake::time+=21000000;Connectivity::poll();
        const auto scheduled=sys::retryAt;
        fake::time+=2100000;Connectivity::poll();CHECK(fake::stopPending);
        fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
        fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();
        if(attempt<3) {
            if(fake::time<scheduled) {
                CHECK(!fake::scan && sys::retryAt==scheduled);
                fake::time=scheduled;Connectivity::poll();
            }
            CHECK(fake::scan);fake::scanDone();Connectivity::poll();
        } else {
            CHECK(Connectivity::snapshot().state==WifiState::Failed);
            CHECK(!sys::reconnect && !sys::connectQueued && !sys::retryAt && !fake::scan);
        }
    }
    CHECK(fake::connects==firstTimeoutConnect+3);
    fake::time+=60000000;Connectivity::poll();CHECK(fake::connects==firstTimeoutConnect+3);stop();
    // A START that arrives after its transition timeout still respects an
    // existing 8-second retry deadline rather than restarting the probe early.
    fresh();start();selectAndAssociate();
    for(unsigned attempt=0;attempt<2;++attempt) {
        fake::time+=21000000;Connectivity::poll();fake::disconnectDone();Connectivity::poll();
        fake::time=sys::retryAt;Connectivity::poll();CHECK(fake::scan);
        fake::scanDone();Connectivity::poll();
    }
    fake::time+=21000000;Connectivity::poll();const auto lateStartRetry=sys::retryAt;
    fake::time+=2100000;Connectivity::poll();CHECK(fake::stopPending);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
    fake::time+=4100000;Connectivity::poll();CHECK(sys::transitionTimedOut);
    CHECK(sys::retryAt==lateStartRetry && fake::time<lateStartRetry);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();
    CHECK(!fake::scan && sys::retryAt==lateStartRetry);
    fake::time=lateStartRetry;Connectivity::poll();CHECK(fake::scan);stop();
    // Failed STOP recovery is quiescent until an explicit retry. That retry
    // re-arms a bounded fence; it must not remain stuck behind a deadline of 0.
    fresh();start();selectAndAssociate();
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    fake::fail="stop";fake::time+=2100000;Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Failed && sys::ignoreDisconnect);
    const auto failedStops=fake::stops;
    for(unsigned i=0;i<20;++i){fake::time+=250000;Connectivity::poll();}
    CHECK(fake::stops==failedStops && !sys::disconnectDeadline);
    fake::fail.clear();CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    CHECK(sys::disconnectDeadline>fake::time);Connectivity::poll();CHECK(!fake::scan);
    fake::time+=2100000;Connectivity::poll();CHECK(fake::stopPending);
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_STOP);Connectivity::poll();
    fake::emit(WIFI_EVENT,WIFI_EVENT_STA_START);Connectivity::poll();CHECK(fake::scan);stop();
    // A network just displayed by the normal scan goes directly to association.
    // A broken directed probe cannot veto that fresh discovery; no extra heap.
    fresh();start();fake::records={makeAp("test-ap",-48,13)};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    const auto displayedScans=fake::scans,displayedConnects=fake::connects,displayedAllocations=fake::allocations;
    fake::dropDirected=true;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::scans==displayedScans && fake::connects==displayedConnects+1);
    CHECK(fake::connectChannel==13 && fake::allocations==displayedAllocations);
    CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN);
    CHECK(Connectivity::snapshot().state==WifiState::Connecting);stop();

    // Cache TTL is enforced both at selection and at actual association.
    for(const auto age:{60000000LL,60000001LL}) {
        fresh();start();fake::records={makeAp("test-ap")};
        CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
        fake::time+=age;const auto startsBefore=fake::connects;
        CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
        CHECK((fake::connects==startsBefore+1)==(age<=60000000));
        CHECK(fake::scan==(age>60000000));
        if(age>60000000) {fake::scanDone();Connectivity::poll();CHECK(fake::connects==startsBefore+1);}
        CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN);stop();
    }
    fresh();start();fake::records={makeAp("test-ap")};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);
    const auto delayedConnects=fake::connects;fake::time+=60000001;Connectivity::poll();
    CHECK(fake::scan && fake::connects==delayedConnects);
    fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==delayedConnects+1 && fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN);stop();

    // A discovered SSID without a channel cannot use the accelerated path.
    // Discovery is still required; the driver receives an unpinned all-channel scan.
    fresh();start();fake::records={makeAp("test-ap",-50,0)};
    const auto unknownChannelConnects=fake::connects;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::scan && fake::connects==unknownChannelConnects);
    fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==unknownChannelConnects+1 && fake::connectChannel==0);
    CHECK(fake::connectConfig.sta.scan_method==WIFI_ALL_CHANNEL_SCAN);stop();

    // A fresh cache is exact by byte length, including 32-byte and UTF-8 SSIDs.
    for(const std::string name:{std::string(32,'a'),std::string("\xe9\x9b\xa8\xe5\xa2\xa8-wifi"),std::string("Test AP ")}) {
        fresh();start();fake::records={makeAp(name,-50,11)};
        CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
        const auto before=fake::scans;
        CHECK(Connectivity::connectWifi(name,"12345678")==ESP_OK);Connectivity::poll();
        CHECK(fake::scans==before && fake::connectChannel==11);
        CHECK(Connectivity::snapshot().state==WifiState::Connecting);stop();
    }
    fresh();start();fake::records={makeAp("test-ap-other")};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::scan && !fake::scanFilter.empty());stop();

    // A cancelled refresh owns its SCAN_DONE even when older fresh results exist.
    fresh();start();fake::records={makeAp("test-ap")};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::startScan()==ESP_OK);
    const auto cancelledConnects=fake::connects;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::connects==cancelledConnects && Connectivity::snapshot().scanPending);
    fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==cancelledConnects+1);stop();

    // Empty directed discovery gets one broadcast fallback. In a dense mesh,
    // the target may lie outside all 24 display rows and must still be found.
    fresh();start();fake::dropDirected=true;
    for(unsigned i=0;i<64;++i)fake::records.push_back(makeAp("neighbor-"+std::to_string(i),-30-int(i/4),1));
    fake::records.push_back(makeAp("test-ap",-95,13));
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    const auto denseConnects=fake::connects;
    CHECK(fake::scanFilter=="test-ap");fake::scanDone();Connectivity::poll();
    CHECK(fake::scan && fake::scanFilter.empty() && fake::connects==denseConnects);
    CHECK(Connectivity::snapshot().retry==0 && Connectivity::snapshot().state==WifiState::Searching);
    Connectivity::cancelUserScan();CHECK(fake::scan && sys::targetScan);
    fake::scanDone();Connectivity::poll();
    CHECK(fake::recordsRead==65 && fake::results.empty());
    CHECK(fake::connects==denseConnects+1 && fake::connectChannel==13);
    CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN);
    std::array<wifi_ap_record_t,Connectivity::kMaxNetworks> visible{};
    CHECK(Connectivity::scanResults(visible.data(),visible.size())==visible.size());
    CHECK(std::none_of(visible.begin(),visible.end(),[](const auto &ap){return std::strcmp(reinterpret_cast<const char *>(ap.ssid),"test-ap")==0;}));
    stop();

    // Repeated SSIDs keep the strongest radio and cannot crowd out other rows.
    fresh();start();fake::records={makeAp("test-ap",-85,1),makeAp("test-ap",-35,11),makeAp("test-ap",-65,6),makeAp("second",-50,6)};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::scanResults(visible.data(),visible.size())==2);
    CHECK(visible[0].primary==11 && visible[0].rssi==-35);
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::connectChannel==11);stop();

    // Truly absent networks exhaust exactly four bounded attempts (each two
    // scans). An empty successful scan must never reach esp_wifi_connect().
    fresh();start();const auto absentConnects=fake::connects,absentScans=fake::scans;
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    for(unsigned i=0;i<4;++i) {
        CHECK(fake::scan && fake::scanFilter=="test-ap");fake::scanDone();Connectivity::poll();
        CHECK(fake::scan && fake::scanFilter.empty());fake::scanDone();Connectivity::poll();
        CHECK(Connectivity::snapshot().error==Connectivity::NetworkNotSeen);
        CHECK(fake::connects==absentConnects && !fake::scan);
        if(i<3) {CHECK(sys::retryAt-fake::time==(30000000LL<<i));fake::time=sys::retryAt;Connectivity::poll();}
    }
    CHECK(fake::scans==absentScans+8 && !sys::retryAt && !sys::connectQueued);
    fake::time+=300000000;Connectivity::poll();CHECK(fake::scans==absentScans+8);stop();

    // Scan completion / record retrieval errors are not an absent network,
    // cannot validate a partial result and must release all driver records.
    for(bool completionError:{false,true}) {
        fresh();start();fake::records={makeAp("test-ap"),makeAp("test-ap",-50,11)};
        CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
        const auto before=fake::connects;
        if(!completionError)fake::recordsFailAfter=1;
        fake::scanDone(completionError?1:0);Connectivity::poll();
        CHECK(Connectivity::snapshot().error==ESP_FAIL && fake::connects==before && fake::results.empty());
        CHECK(sys::networksSeenAt==-1 && !sys::targetValidated && !sys::broadcastProbe);stop();
    }

    // A known radio can vanish between discovery and association. Its driver
    // error remains distinct, and retry discards cache/channel and re-probes.
    fresh();start();fake::records={makeAp("test-ap",-50,11)};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN && fake::connectChannel==11);
    fake::disconnectDone(WIFI_REASON_NO_AP_FOUND);Connectivity::poll();
    CHECK(Connectivity::snapshot().error==WIFI_REASON_NO_AP_FOUND && sys::targetChannel==0);
    CHECK(sys::retryAt-fake::time==2000000);
    fake::time=sys::retryAt;Connectivity::poll();CHECK(fake::scan);
    const auto changedChannelConnects=fake::connects;
    fake::records={makeAp("test-ap",-45,1)};fake::scanDone();Connectivity::poll();
    CHECK(fake::connects==changedChannelConnects+1 && fake::connectChannel==1);
    CHECK(fake::connectConfig.sta.scan_method==WIFI_ALL_CHANNEL_SCAN && Connectivity::snapshot().retry==1);
    associated();Connectivity::poll();fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Connected && Connectivity::snapshot().retry==0);stop();

    // Authentication failures do not trigger an automatic fallback that keeps
    // retrying rejected credentials. A later explicit request starts a new fast attempt.
    for(const auto reason:{WIFI_REASON_AUTH_FAIL,WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,WIFI_REASON_HANDSHAKE_TIMEOUT}) {
        fresh();start();selectAndAssociate();
        CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN);
        const auto rejectedConnects=fake::connects,rejectedScans=fake::scans;
        fake::disconnectDone(reason);Connectivity::poll();
        CHECK(Connectivity::snapshot().state==WifiState::Failed && Connectivity::snapshot().error==reason);
        CHECK(!sys::retryAt && !sys::reconnect && !sys::connectQueued);
        fake::time+=300000000;Connectivity::poll();
        CHECK(fake::connects==rejectedConnects && fake::scans==rejectedScans);
        selectAndAssociate();CHECK(fake::connects==rejectedConnects+1);
        CHECK(fake::connectConfig.sta.scan_method==WIFI_FAST_SCAN && Connectivity::snapshot().retry==0);stop();
    }

    // Timings separate discovery, SDK join and DHCP instead of attributing the
    // whole wait to DHCP. Unchanged polls and duplicate ASSOCIATED do not log.
    fresh();start();fake::records={makeAp("test-ap",-40,6)};
    const std::string privatePassword="private-test-password";
    CHECK(Connectivity::connectWifi("test-ap",privatePassword)==ESP_OK);Connectivity::poll();
    fake::time+=2300000;fake::scanDone();Connectivity::poll();
    CHECK(hasLog("connect requested attempt=1 cached=0 channel=0"));
    CHECK(hasLog("probe done attempt=1 mode=directed result=0 found=1 records=1 scan_ms=2300 elapsed_ms=2300"));
    CHECK(hasLog("join start attempt=1 scan=fast channel=6 prejoin_ms=2300"));
    auto logCount=fake::logLines.size();
    for(unsigned i=0;i<100;++i)Connectivity::poll();
    CHECK(fake::logLines.size()==logCount);
    fake::time+=1250000;associated();Connectivity::poll();
    CHECK(hasLog("associated attempt=1 join_ms=1250 elapsed_ms=3550"));
    logCount=fake::logLines.size();associated();Connectivity::poll();CHECK(fake::logLines.size()==logCount);
    fake::time+=400000;fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(hasLog("online attempt=1 dhcp_ms=400 total_ms=3950"));
    for(const auto &line:fake::logLines) {
        CHECK(line.find("test-ap")==std::string::npos);
        CHECK(line.find(privatePassword)==std::string::npos);
        CHECK(line.find("192.0.2.1")==std::string::npos);
    }
    stop();

    // Default dwell is applied to the internal connection scan, including the
    // fresh-cache path. A failed SDK setter cannot fall through to connect.
    fresh();start();fake::records={makeAp("test-ap")};
    CHECK(Connectivity::startScan()==ESP_OK);fake::scanDone();Connectivity::poll();
    const auto paramsBefore=fake::scanParameterCalls,connectsBeforeParams=fake::connects;
    fake::fail="scan-defaults";
    CHECK(Connectivity::connectWifi("test-ap","12345678")==ESP_OK);Connectivity::poll();
    CHECK(fake::scanParameterCalls==paramsBefore+1 && fake::connects==connectsBeforeParams);
    CHECK(Connectivity::snapshot().state==WifiState::Failed && Connectivity::snapshot().error==ESP_FAIL);
    CHECK(!sys::connectionActive && !sys::targetValidated);
    fake::fail.clear();selectAndAssociate();
    CHECK(fake::connects==connectsBeforeParams+1 && fake::scanParameterCalls==paramsBefore+2);stop();

    // A slow association gets its own full DHCP budget. Duplicate notifications
    // must not keep extending that budget, and expired DHCP cannot resurrect it.
    fresh();start();selectAndAssociate();
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::Joining);
    fake::time+=19000000;Connectivity::poll();CHECK(Connectivity::snapshot().state==WifiState::Connecting);
    associated();Connectivity::poll();
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::Address);
    const auto addressDeadline=sys::connectDeadline;
    CHECK(addressDeadline-fake::time==15000000);
    fake::time+=5000000;associated();Connectivity::poll();CHECK(sys::connectDeadline==addressDeadline);
    fake::time=addressDeadline-1;Connectivity::poll();CHECK(Connectivity::snapshot().state==WifiState::Connecting);
    fake::time=addressDeadline;Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Failed && Connectivity::snapshot().error==ESP_ERR_TIMEOUT);
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::Address && fake::disconnectRequested);
    associated();fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Failed && sys::connectDeadline==0);stop();

    // The same reason code is diagnosed at the phase which actually failed.
    for(const auto phase:{WifiConnectPhase::Joining,WifiConnectPhase::Address,WifiConnectPhase::Online}) {
        fresh();start();selectAndAssociate();
        if(phase!=WifiConnectPhase::Joining){associated();Connectivity::poll();}
        if(phase==WifiConnectPhase::Online){fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();}
        CHECK(Connectivity::snapshot().phase==phase);
        fake::disconnectDone(WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY);Connectivity::poll();
        CHECK(Connectivity::snapshot().state==WifiState::Failed && Connectivity::snapshot().error==4);
        CHECK(Connectivity::snapshot().phase==phase);
        CHECK(sys::retryAt-fake::time==2000000);stop();
    }

    // Queued ASSOCIATED followed by a physical disconnect is not live DHCP.
    fresh();start();selectAndAssociate();associated();
    fake::disconnectDone(WIFI_REASON_NO_AP_FOUND);Connectivity::poll();
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::Joining);
    CHECK(Connectivity::snapshot().error==WIFI_REASON_NO_AP_FOUND);stop();

    // Wrong-target, cancelled and late ASSOCIATED notifications never advance
    // or revive an operation. No new unbounded retry loop is introduced.
    fresh();start();selectAndAssociate();
    const auto originalDeadline=sys::connectDeadline;
    associated("other-ap");Connectivity::poll();
    CHECK(Connectivity::snapshot().phase==WifiConnectPhase::Joining && sys::connectDeadline==originalDeadline);
    CHECK(Connectivity::disconnectWifi()==ESP_OK);associated();Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Disconnected && Connectivity::snapshot().phase==WifiConnectPhase::None);
    fake::disconnectDone();Connectivity::poll();associated();Connectivity::poll();
    CHECK(Connectivity::snapshot().state==WifiState::Disconnected && !sys::retryAt && !sys::connectDeadline);stop();

    // Borrow an online STA, survive an upstream dropout, and preserve it on exit.
    fresh();start();selectAndAssociate();associated();Connectivity::poll();
    fake::hasIp=true;fake::emit(IP_EVENT,IP_EVENT_STA_GOT_IP);Connectivity::poll();
    CHECK(Connectivity::acquireForHotspot()==ESP_OK);
    CHECK(Connectivity::hotspotUplink().online&&Connectivity::hotspotUplink().dns==3);
    CHECK(Connectivity::useStationRoute()==ESP_OK);
    const auto sharedStops=fake::stops;
    fake::disconnectDone(WIFI_REASON_NO_AP_FOUND);Connectivity::poll();CHECK(!Connectivity::hotspotUplink().online);
    CHECK(fake::stops==sharedStops&&sys::retryAt);
    sys::state.retry=3;sys::reconnect=true;sys::failed(WIFI_REASON_NO_AP_FOUND);
    CHECK(sys::retryAt-fake::time==60000000&&sys::reconnect);
    CHECK(sys::restartStationLocked(fake::time)==Connectivity::HotspotBusy&&fake::stops==sharedStops);
    sys::failed(WIFI_REASON_AUTH_FAIL);CHECK(!sys::retryAt&&!sys::reconnect);
    Connectivity::releaseFromHotspot();stop();
    std::printf("%u checks, %u lifecycle/failure scenarios passed; synthetic IDF only\n",checks,scenarios);
}
