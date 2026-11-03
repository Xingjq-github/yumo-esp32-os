#include "core/xiaozhi_cloud.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace test {
unsigned checks = 0, scenarios = 0;
void check(bool value, const char* label) { ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
struct Reply {
    int status = 200;
    std::string body;
    int64_t length = -2;
    bool complete = true;
    int openError = 0, writeChunk = 4096, readChunk = 1024;
    bool readError = false;
};
struct Request { std::string url,body; std::map<std::string,std::string> headers; };
struct Machine {
    int64_t now = 1000000;
    bool screen = true, sleep = false, wifi = true, cancelled = false;
    bool failInit = false, failHeader = false, failNvsOpen = false, failMac = false;
    int nvsError = 0, commitError = 0, nvsReads = 0, nvsWrites = 0, commits = 0, nvsCloses = 0;
    unsigned clients = 0, cleanups = 0;
    std::string storedUuid, interruptStage;
    int interruptKind = 0;
    unsigned slowReads = 0, callbacks = 0;
    std::vector<Reply> replies;
    std::vector<Request> requests;
} machine;
void stage(const char* name) {
    machine.now += 1000;
    if (machine.interruptStage == name) {
        if (machine.interruptKind == 1) machine.screen = false;
        else if (machine.interruptKind == 2) machine.sleep = true;
        else if (machine.interruptKind == 3) machine.wifi = false;
        else machine.cancelled = true;
    }
}
void reset() { ++scenarios; machine = Machine{}; }
const std::string ws = R"({"websocket":{"url":"wss://voice.example.test/v1","token":"synthetic-token","version":1}})";
const std::string activation = R"({"activation":{"code":"123456","message":"Synthetic activation","challenge":"synthetic","timeout_ms":15000}})";
}
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, NVS_READWRITE=2, ESP_MAC_WIFI_STA=3, HTTP_METHOD_POST=4;
using nvs_handle_t = unsigned;
esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* out) {
    test::check(std::string(ns)=="yumo_xiaozhi" && mode==NVS_READWRITE, "dedicated NVS namespace");
    *out=7; return test::machine.failNvsOpen ? -1 : ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t, const char* key, char* out, size_t* size) {
    ++test::machine.nvsReads;
    test::check(std::string(key)=="client_id", "dedicated client UUID key");
    if(test::machine.nvsError) return test::machine.nvsError;
    if(test::machine.storedUuid.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if(*size < test::machine.storedUuid.size()+1) return -2;
    *size=test::machine.storedUuid.size()+1;
    std::memcpy(out,test::machine.storedUuid.c_str(),*size);
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t, const char* key, const char* value) {
    test::check(std::string(key)=="client_id", "only client UUID is persisted");
    ++test::machine.nvsWrites; test::machine.storedUuid=value; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { ++test::machine.commits; return test::machine.commitError; }
void nvs_close(nvs_handle_t) { ++test::machine.nvsCloses; }
void esp_fill_random(void* out,size_t size) { for(size_t i=0;i<size;++i) static_cast<uint8_t*>(out)[i]=uint8_t(i); }
esp_err_t esp_read_mac(uint8_t* out,int kind) { test::check(kind==ESP_MAC_WIFI_STA,"station MAC identity"); std::memset(out,0,6); return test::machine.failMac?-1:ESP_OK; }
struct esp_app_desc_t { const char* version="synthetic-yumo"; const char* idf_ver="test-sdk"; };
const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t value; return &value; }
int64_t esp_timer_get_time() { return test::machine.now; }
unsigned pdMS_TO_TICKS(unsigned value) { return value; }
void vTaskDelay(unsigned ms) { test::machine.now+=int64_t(ms)*1000; test::stage(ms >= 50 ? "wait" : "delay"); }
namespace sys {
enum class WifiState { Connected, Disconnected };
struct Connectivity { static WifiState wifiState() { return test::machine.wifi?WifiState::Connected:WifiState::Disconnected; } };
struct Power {
    static bool isScreenOn() { return test::machine.screen; }
    static bool sleepPending() { return test::machine.sleep; }
};
}
void esp_crt_bundle_attach() {}
struct esp_http_client_config_t {
    const char* url=nullptr;
    int method=0, timeout_ms=0, buffer_size=0, buffer_size_tx=0;
    void (*crt_bundle_attach)()=nullptr;
    bool disable_auto_redirect=false,skip_cert_common_name_check=true;
};
struct Http { size_t request=0, offset=0; test::Reply reply; };
using esp_http_client_handle_t=Http*;
Http* esp_http_client_init(const esp_http_client_config_t* cfg) {
    test::check(cfg->method==HTTP_METHOD_POST && cfg->timeout_ms==5000,"POST with bounded I/O timeout");
    test::check(cfg->crt_bundle_attach==esp_crt_bundle_attach && cfg->disable_auto_redirect && !cfg->skip_cert_common_name_check,"CA+host verification and no redirect");
    test::check(std::string(cfg->url).rfind("https://api.tenclass.net/xiaozhi/ota/",0)==0,"official HTTPS endpoint only");
    if(test::machine.failInit) return nullptr;
    ++test::machine.clients;
    auto* http=new Http;
    http->request=test::machine.requests.size();
    test::machine.requests.push_back(test::Request{cfg->url,{},{}});
    return http;
}
int esp_http_client_set_header(Http* h,const char* key,const char* value) {
    test::machine.requests[h->request].headers[key]=value;
    return test::machine.failHeader?-1:ESP_OK;
}
int esp_http_client_open(Http* h,int) {
    test::check(h->request<test::machine.replies.size(),"expected cloud request sequence");
    h->reply=test::machine.replies[h->request]; test::stage("open"); return h->reply.openError;
}
int esp_http_client_write(Http* h,const char* data,int size) {
    test::stage("write");
    int count=std::min(size,h->reply.writeChunk);
    if(count>0) test::machine.requests[h->request].body.append(data,size_t(count));
    return count;
}
int64_t esp_http_client_fetch_headers(Http* h) { test::stage("headers"); return h->reply.length==-2?int64_t(h->reply.body.size()):h->reply.length; }
int esp_http_client_get_status_code(Http* h) { return h->reply.status; }
int esp_http_client_read(Http* h,char* data,int capacity) {
    test::stage(h->offset==h->reply.body.size()?"eof":"read");
    if(test::machine.slowReads) test::machine.now+=int64_t(test::machine.slowReads)*1000000;
    if(h->reply.readError) return -1;
    const size_t count=std::min({size_t(capacity),size_t(h->reply.readChunk),h->reply.body.size()-h->offset});
    std::memcpy(data,h->reply.body.data()+h->offset,count);h->offset+=count;return int(count);
}
bool esp_http_client_is_complete_data_received(Http* h) { return h->reply.complete; }
void esp_http_client_close(Http*) {}
void esp_http_client_cleanup(Http* h) { ++test::machine.cleanups; delete h; }

// Same production implementation, with only platform #include directives adapted.
#include "xiaozhi_cloud.inc"

namespace test {
bool run(core::xiaozhi::CloudConfig& config,std::string& error) {
    const bool ok=core::xiaozhi::discover(config,[](){return machine.cancelled;},[](const std::string& code,const std::string&){
        ++machine.callbacks;check(code=="123456","user receives synthetic activation code");
    },error);
    check(machine.clients==machine.cleanups,"every HTTP client released");
    return ok;
}
}
int main() {
    using namespace test;
    using namespace core::xiaozhi;
    CloudConfig config;std::string error;
    reset();machine.replies={Reply{200,ws}};
    check(run(config,error),"ready discovery succeeds");
    check(machine.nvsWrites==1 && machine.commits==1 && machine.nvsCloses==1,"UUID persisted exactly once");
    check(config.clientId.size()==36 && config.clientId[14]=='4' && config.clientId[19]=='8',"stable UUIDv4 format");
    check(machine.requests.size()==1 && machine.requests[0].headers.at("Activation-Version")=="1","v1 activation request");
    const auto firstUuid=config.clientId;
    auto* body=cJSON_Parse(machine.requests[0].body.c_str());
    auto* board=cJSON_GetObjectItemCaseSensitive(body,"board");
    check(board && std::string(cJSON_GetObjectItemCaseSensitive(board,"type")->valuestring)=="esp32-s3-touch-lcd-1.54","official exact board identity");
    check(!cJSON_GetObjectItemCaseSensitive(board,"ssid") && !cJSON_GetObjectItemCaseSensitive(body,"vault"),"no WiFi/password vault upload");cJSON_Delete(body);
    reset();machine.storedUuid=firstUuid;machine.replies={Reply{200,ws}};
    check(run(config,error) && config.clientId==firstUuid && machine.nvsWrites==0,"return visit reuses UUID");
    reset();machine.replies={Reply{200,activation},Reply{202,"{}"},Reply{200,"{}"},Reply{200,ws}};
    check(run(config,error),"202 waiting then200 activation then refetch");
    check(machine.callbacks==1 && machine.requests.size()==4,"activation callback and bounded polling");
    check(machine.requests[1].url.find("/activate")!=std::string::npos && machine.requests[1].body=="{}" && machine.requests[2].body=="{}","v1 activation payload");
    check(machine.requests[3].url.back()=='/' && config.token=="synthetic-token","fresh credentials discovered after activation");
    for(int mode=0;mode<4;++mode) for(const auto& step:{"open","write","headers","read","eof"}) {
        reset();machine.replies={Reply{200,ws}};machine.interruptStage=step;machine.interruptKind=mode;
        check(!run(config,error),"cancel/screen-off/sleep/WiFi loss at each blocking boundary");
    }
    reset();machine.replies={Reply{200,activation},Reply{202,"{}"}};machine.interruptStage="wait";
    check(!run(config,error) && machine.requests.size()==2,"cancel during activation delay without next request");
    for(int status:{301,302,307,400,401,403,429,500,502,503}) {
        reset();machine.replies={Reply{status,"{}"}};
        check(!run(config,error) && machine.requests.size()==1,"HTTP errors do not redirect or leak identity");
    }
    reset();machine.replies={Reply{200,ws}};machine.replies[0].writeChunk=3;machine.replies[0].readChunk=5;
    check(run(config,error),"partial writes and fragmented response");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].complete=false;
    check(!run(config,error),"incomplete response never accepted");
    reset();machine.replies={Reply{200,std::string(MaxJsonBytes+1,' ')}};machine.replies[0].length=0;
    check(!run(config,error),"chunked oversized response bounded");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].length=MaxJsonBytes+1;
    check(!run(config,error),"oversized content length rejected before read");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].length=-1;
    check(!run(config,error),"header fetch timeout rejected");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].readError=true;
    check(!run(config,error),"response read error");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].readChunk=1;machine.slowReads=5;
    check(!run(config,error) && machine.now<37000000,"30s overall request deadline");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].openError=-1;
    check(!run(config,error),"TLS or connect failure");
    reset();machine.replies={Reply{200,ws}};machine.replies[0].writeChunk=0;
    check(!run(config,error),"zero write rejected");
    reset();machine.failInit=true;check(!run(config,error),"HTTP init allocation failure");
    reset();machine.failHeader=true;check(!run(config,error),"header allocation failure");
    reset();machine.failNvsOpen=true;check(!run(config,error) && machine.requests.empty(),"NVS open failure before network");
    reset();machine.nvsError=-2;check(!run(config,error) && machine.nvsWrites==0,"corrupt UUID not silently regenerated");
    reset();machine.storedUuid="invalid";check(!run(config,error) && machine.nvsWrites==0,"malformed UUID not silently regenerated");
    reset();machine.commitError=-1;check(!run(config,error) && machine.requests.empty(),"NVS commit failure before network");
    reset();machine.failMac=true;check(!run(config,error) && machine.requests.empty(),"MAC failure before network");
    reset();machine.wifi=false;check(!run(config,error) && machine.nvsReads==0,"offline entry no identity/network work");
    reset();machine.screen=false;check(!run(config,error) && machine.nvsReads==0,"screen-off entry no work");
    reset();machine.cancelled=true;check(!run(config,error) && machine.nvsReads==0,"cancelled entry no work");
    // A perpetually pending activation reaches the app's bounded lifetime.
    reset();
    for(unsigned i=0;i<25;++i) {
        machine.replies.push_back(Reply{200,activation});
        for(unsigned j=0;j<5;++j) machine.replies.push_back(Reply{202,"{}"});
    }
    check(!run(config,error) && machine.now<305000000,"five-minute activation bound");
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u}\n",scenarios,checks);
}
