// Runs the unmodified production Http class with deterministic transport events.
#include "core/speed_test.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <strings.h>
#include <iostream>
#define ESP_LOGI(...) ((void)0)
using namespace core::speed;
using esp_err_t=int;
constexpr int ESP_OK=0,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
enum {HTTP_METHOD_GET,HTTP_METHOD_POST,HTTP_EVENT_ON_CONNECTED,HTTP_EVENT_ON_HEADERS_COMPLETE,HTTP_EVENT_ON_HEADER};
struct esp_http_client_event_t {int event_id;void *user_data;const char *header_key=nullptr,*header_value=nullptr;};
struct esp_http_client_config_t {
    const char *url;esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data;
    int (*crt_bundle_attach)(void *);bool skip_cert_common_name_check,disable_auto_redirect,keep_alive_enable;
    int timeout_ms,buffer_size,buffer_size_tx;
};
struct Transport {
    esp_http_client_config_t config{};
    bool connected=false,chunked=false,complete=true,compress=false,closeHeader=false,network=true;
    void (*onRead)()=nullptr;
    int status=200,openError=0,headerError=0,writeLimit=3000,readLimit=2048,ackMode=0;
    int requested=0,remaining=0,method=0,closes=0,reads=0,writes=0;
    int64_t now=1000,connectTime=200000,headerTime=100000,readTime=1000;
} transport;
using esp_http_client_handle_t=Transport *;
int cleanups=0,allocations=0;
int64_t esp_timer_get_time(){return transport.now;}
int esp_crt_bundle_attach(void *){return 0;}
void *heap_caps_malloc(size_t n,int){++allocations;return std::malloc(n);}
void heap_caps_free(void *p){if(p)--allocations;std::free(p);}
void esp_fill_random(void *p,size_t n){std::memset(p,0x5a,n);}
unsigned esp_random(){return 1;}
void vTaskDelay(int){transport.now+=1000;}
void publish(Job &job,const Snapshot &s){job.published=s;}
bool stopped(Job &job){return job.cancelled.load();}
bool sameNetwork(const Record &){return transport.network;}
void cleanText(char *out,size_t n,const char *in){std::snprintf(out,n,"%s",in);}
void event(int id,const char *key=nullptr,const char *value=nullptr){
    esp_http_client_event_t e{id,transport.config.user_data,key,value};transport.config.event_handler(&e);
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config){
    transport.config=*config;assert(config->crt_bundle_attach && !config->skip_cert_common_name_check && config->disable_auto_redirect);return &transport;
}
int esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *){return 0;}
int esp_http_client_set_url(esp_http_client_handle_t,const char *url){auto p=std::strstr(url,"bytes=");transport.requested=p?std::atoi(p+6):0;return 0;}
int esp_http_client_set_method(esp_http_client_handle_t,int method){transport.method=method;return 0;}
int esp_http_client_open(esp_http_client_handle_t,int bytes){
    if(transport.openError)return transport.openError;
    if(!transport.connected){transport.now+=transport.connectTime;event(HTTP_EVENT_ON_CONNECTED);transport.connected=true;}
    transport.now+=1000;if(transport.method==HTTP_METHOD_POST)transport.requested=bytes;
    transport.remaining=transport.method==HTTP_METHOD_POST?0:transport.requested;return 0;
}
int esp_http_client_write(esp_http_client_handle_t,const char *,int n){
    assert(n<=2048);++transport.writes;transport.now+=1000;return std::min(n,transport.writeLimit);
}
int esp_http_client_fetch_headers(esp_http_client_handle_t){
    transport.now+=transport.headerTime;
    if(transport.headerError){transport.status=-1;return transport.headerError;}
    if(transport.compress)event(HTTP_EVENT_ON_HEADER,"Content-Encoding","gzip");
    if(transport.closeHeader)event(HTTP_EVENT_ON_HEADER,"Connection","close");
    event(HTTP_EVENT_ON_HEADER,"CF-RAY","host-test-FRA");
    if(transport.method==HTTP_METHOD_POST && transport.ackMode!=1){
        char ack[40];std::snprintf(ack,sizeof(ack),"%d",transport.requested+(transport.ackMode==2));
        event(HTTP_EVENT_ON_HEADER,"cf-meta-upload-bytes",transport.ackMode==4?"999999999999999999999999999999":ack);
        if(transport.ackMode==3)event(HTTP_EVENT_ON_HEADER,"cf-meta-upload-bytes",ack);
    }
    event(HTTP_EVENT_ON_HEADERS_COMPLETE);return transport.remaining;
}
int esp_http_client_get_status_code(esp_http_client_handle_t){return transport.status;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t){return transport.chunked;}
int esp_http_client_read(esp_http_client_handle_t,char *,int n){
    ++transport.reads;transport.now+=transport.readTime;
    if(transport.onRead)transport.onRead();
    int got=std::min({n,transport.readLimit,transport.remaining});transport.remaining-=got;return got;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t){return transport.complete && !transport.remaining;}
bool esp_http_client_is_persistent_connection(esp_http_client_handle_t){return transport.connected;}
int esp_http_client_close(esp_http_client_handle_t){++transport.closes;transport.connected=false;return 0;}
int esp_http_client_cleanup(esp_http_client_handle_t){++cleanups;return 0;}
#include "production_http.inc"
void reset(){transport=Transport{};}
int main(){
    reset();
    {
        Job job;Snapshot state;Http http(job,state);assert(bool(http));
        auto s=http.request(false,8192,RunLimitUs);
        assert(s.complete && !s.reused && s.connectUs==200000 && s.headersUs==101000 && s.elapsedUs==107000 && s.bytes==8192);
        assert(state.record.transferredBytes==8192 && !std::strcmp(state.record.serverTrace,"host-test-FRA"));
        s=http.request(false,0,RunLimitUs);assert(s.complete && s.reused && s.connectUs==0 && s.bytes==0 && s.headersUs==101000);
        transport.writeLimit=1500;
        s=http.request(true,8192,RunLimitUs);assert(s.complete && s.acknowledged==8192 && transport.writes==6);
        assert(state.record.transferredBytes==16384);
        transport.closeHeader=true;s=http.request(false,10,RunLimitUs);assert(s.complete && !transport.connected);
    }
    assert(allocations==0 && cleanups==1);
    for(int fail:{0,-1}){reset();Job job;Snapshot state;Http http(job,state);transport.writeLimit=fail;
        auto s=http.request(true,FirstBytes,RunLimitUs);
        assert(!s.complete && s.error==Error::Write && !s.bytes && !s.acknowledged && transport.writes==1 && transport.closes==1);
    }
    for(int ack=1;ack<=4;++ack){reset();Job job;Snapshot state;Http http(job,state);transport.ackMode=ack;auto s=http.request(true,8192,RunLimitUs);assert(!s.complete && s.error==Error::UploadAck && transport.closes==1);}
    for(int failure=0;failure<9;++failure){
        reset();Job job;Snapshot state;Http http(job,state);Error expected;
        switch(failure){
        case 0:transport.status=403;expected=Error::Http;break;
        case 1:transport.compress=true;expected=Error::Encoding;break;
        case 2:transport.readLimit=0;expected=Error::ResponseLength;break;
        case 3:transport.complete=false;expected=Error::ResponseLength;break;
        case 4:transport.openError=-17;expected=Error::Connect;break;
        case 5:job.cancelled=true;expected=Error::Cancelled;break;
        case 6:transport.network=false;expected=Error::NetworkChanged;break;
        case 7:transport.readTime=RequestLimitUs;expected=Error::Timeout;break;
        default:transport.headerError=-17;expected=Error::Headers;break;
        }
        auto s=http.request(false,8192,RunLimitUs);assert(!s.complete && s.error==expected && transport.closes==1);
        if(failure==8)assert(s.httpStatus==0 && s.nativeError==-17);
    }
    reset();{Job job;Snapshot state;Http http(job,state);auto s=http.request(false,0,transport.now);assert(!s.complete && s.error==Error::Timeout);}
    reset();{Job job;Snapshot state;Http http(job,state);transport.onRead=[](){transport.network=false;};auto s=http.request(false,8192,RunLimitUs);assert(!s.complete && s.error==Error::NetworkChanged);}
    reset();{static Job job;Snapshot state;Http http(job,state);transport.onRead=[](){job.cancelled=true;};auto s=http.request(false,8192,RunLimitUs);assert(!s.complete && s.error==Error::Cancelled && transport.reads==1);}
    reset();{Job job;Snapshot state;state.phase=Phase::Download;Http http(job,state);transport.readTime=250000;
        auto s=http.request(false,64000,RunLimitUs);assert(s.complete && state.liveValid && state.liveCount>2 && state.liveCount<=40);
        assert(state.liveKbps[0]==-1 && state.liveKbps[1]>0);const auto accepted=throughput(state.record.download,0);assert(!accepted.valid);
        const auto firstCount=state.liveCount;s=http.request(false,64000,RunLimitUs);assert(s.complete && state.liveCount<=40 && state.liveCount>=firstCount);
    }
    assert(allocations==0);
    std::cout<<"Production HTTP: fresh/reused timing, partial writes, server byte ACK, bad/duplicate/missing ACK, status/encoding/short-read failures, deadlines and cleanup passed\n";
}
