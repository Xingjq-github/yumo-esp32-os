// The real transport source is included below; only external SDK/I/O is fake.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#define private public
#include "xiaozhi_transport.hpp"
#undef private

static std::atomic<unsigned> checks{0};
static unsigned scenarios=0;
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": "+#x);}while(0)
using Handler=void(*)(void*,const char*,int32_t,void*);
constexpr int ESP_OK=0, WEBSOCKET_EVENT_ANY=-1, WEBSOCKET_EVENT_CONNECTED=1,
    WEBSOCKET_EVENT_DISCONNECTED=2, WEBSOCKET_EVENT_CLOSED=3, WEBSOCKET_EVENT_ERROR=4,
    WEBSOCKET_EVENT_DATA=5, MQTT_EVENT_ANY=-1, MQTT_EVENT_CONNECTED=1,
    MQTT_EVENT_DISCONNECTED=2, MQTT_EVENT_ERROR=3, MQTT_EVENT_DATA=4;
constexpr int MQTT_TRANSPORT_OVER_SSL=4, MQTT_PROTOCOL_V_3_1_1=4;
constexpr int pdPASS=1;
using TickType_t=unsigned;
using esp_err_t=int;
int esp_crt_bundle_attach(void*) {return ESP_OK;}
struct esp_websocket_client_config_t {
    const char *uri=nullptr,*headers=nullptr;
    esp_err_t (*crt_bundle_attach)(void*)=nullptr;
    bool disable_auto_reconnect=false;
    int network_timeout_ms=0,reconnect_timeout_ms=0,buffer_size=0,task_stack=0,task_prio=0;
};
struct esp_websocket_event_data_t {
    const char *data_ptr=nullptr;
    int data_len=0,payload_len=0,payload_offset=0;
    bool fin=true;
    uint8_t op_code=1;
};
struct esp_mqtt_client_config_t {
    struct {
        struct {const char *hostname=nullptr;unsigned port=0;int transport=0;} address;
        struct {esp_err_t (*crt_bundle_attach)(void*)=nullptr;} verification;
    } broker;
    struct {
        const char *client_id=nullptr,*username=nullptr;
        struct {const char *password=nullptr;} authentication;
    } credentials;
    struct {unsigned keepalive=0;int protocol_ver=0;} session;
    struct {bool disable_auto_reconnect=false;int timeout_ms=0;} network;
    struct {int stack_size=0,priority=0;} task;
    struct {int size=0,out_size=0;} buffer;
};
struct esp_mqtt_event_t {const char *data=nullptr;int data_len=0,total_data_len=0,current_data_offset=0;};
struct FakeWs {Handler callback=nullptr;void *arg=nullptr;};
struct FakeMqtt {Handler callback=nullptr;void *arg=nullptr;};
using esp_websocket_client_handle_t=FakeWs*;
using esp_mqtt_client_handle_t=FakeMqtt*;
struct ip_addr_t {uint32_t value=0;bool ipv4=true;};
using err_t=int;
constexpr int ERR_OK=0,ERR_INPROGRESS=-5,LWIP_DNS_ADDRTYPE_IPV4=0;
using DnsCallback=void(*)(const char*,const ip_addr_t*,void*);
constexpr int AF_INET=2,SOCK_DGRAM=2,IPPROTO_UDP=17,F_SETFL=4,O_NONBLOCK=0x800,MSG_DONTWAIT=64;
struct sockaddr {uint16_t family;};
struct sockaddr_in {uint16_t sin_family=0,sin_port=0;struct {uint32_t s_addr=0;} sin_addr;};
using psa_key_id_t=uint32_t;
constexpr psa_key_id_t PSA_KEY_ID_NULL=0;
constexpr int PSA_SUCCESS=0,PSA_ALG_CTR=1,PSA_KEY_USAGE_ENCRYPT=1,PSA_KEY_USAGE_DECRYPT=2,PSA_KEY_TYPE_AES=4;
struct psa_cipher_operation_t {psa_key_id_t key=0;std::array<uint8_t,16> iv{};bool active=false;};
struct psa_key_attributes_t {int usage=0,alg=0,type=0,bits=0;};
#define PSA_CIPHER_OPERATION_INIT {}
#define PSA_KEY_ATTRIBUTES_INIT {}
#define pdMS_TO_TICKS(x) (x)
#define IP_IS_V4(x) ((x)->ipv4)
#define IP_IS_V4_VAL(x) ((x).ipv4)
#define ip_2_ip4(x) (x)
#define ip4_addr_get_u32(x) ((x)->value)

namespace fake {
int64_t now=0;
std::string fault,headers,uri,host;
bool autoConnect=true;
int liveWs=0,liveMqtt=0,wsStop=0,mqttStop=0,subscribes=0,outbox=0,abortCalls=0,cryptoLive=0;
unsigned port=0;
FakeWs *ws=nullptr;
FakeMqtt *mqtt=nullptr;
std::vector<std::string> messages;
std::vector<std::vector<uint8_t>> sent;
std::deque<std::vector<uint8_t>> received;
std::set<int> sockets;
std::map<psa_key_id_t,std::array<uint8_t,16>> keys;
psa_key_id_t nextKey=1;
int nextSocket=10;
DnsCallback pendingDns=nullptr;
void *pendingDnsArg=nullptr;
int dnsMode=0; // 0 immediate success, 1 failed, 2 pending
std::function<void()> delayHook;
std::function<void()> taskTick;
void (*taskEntry)(void*)=nullptr;
void *taskArg=nullptr;
bool taskActive=false;
void dnsFinish(bool ok=true) {
    CHECK(pendingDns!=nullptr);
    auto callback=pendingDns;auto arg=pendingDnsArg;
    pendingDns=nullptr;pendingDnsArg=nullptr;
    ip_addr_t ip{0x010200c0,true};callback(nullptr,ok?&ip:nullptr,arg);
}
void reset() {
    CHECK(liveWs==0 && liveMqtt==0 && sockets.empty() && keys.empty() && cryptoLive==0 && !pendingDns && !taskActive);
    ++scenarios;now=0;fault.clear();headers.clear();uri.clear();host.clear();autoConnect=true;
    wsStop=mqttStop=subscribes=outbox=abortCalls=0;port=0;messages.clear();sent.clear();received.clear();
    dnsMode=0;delayHook={};taskTick={};taskEntry=nullptr;taskArg=nullptr;
}
}
int64_t esp_timer_get_time(){return fake::now;}
void vTaskDelay(unsigned ms){fake::now+=int64_t(ms)*1000;if(fake::delayHook)fake::delayHook();if(fake::taskTick)fake::taskTick();}
int xTaskCreate(void(*entry)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void**) {
    CHECK(std::string(name)=="xiaozhi_udp" && stack==4096 && priority==4 && !fake::taskActive);
    if(fake::fault=="task_create")return 0;
    fake::taskEntry=entry;fake::taskArg=arg;fake::taskActive=true;return pdPASS;
}
void vTaskDelete(void *task){CHECK(!task && fake::taskActive);fake::taskActive=false;}

FakeWs *esp_websocket_client_init(const esp_websocket_client_config_t *c) {
    CHECK(c->crt_bundle_attach==esp_crt_bundle_attach && c->disable_auto_reconnect);
    CHECK(c->network_timeout_ms==5000 && c->buffer_size==2048 && c->task_stack>=4096);
    fake::headers=c->headers;fake::uri=c->uri;
    if(fake::fault=="ws_init")return nullptr;
    ++fake::liveWs;return fake::ws=new FakeWs;
}
int esp_websocket_register_events(FakeWs *w,int event,Handler cb,void *arg) {
    CHECK(event==WEBSOCKET_EVENT_ANY);w->callback=cb;w->arg=arg;
    return fake::fault=="ws_register"?-1:ESP_OK;
}
int esp_websocket_client_start(FakeWs *w) {
    if(fake::fault=="ws_start")return -1;
    if(fake::autoConnect)w->callback(w->arg,nullptr,WEBSOCKET_EVENT_CONNECTED,nullptr);
    return ESP_OK;
}
int esp_websocket_client_stop(FakeWs *w) {
    ++fake::wsStop;
    esp_websocket_event_data_t d{"late",4,4,0,true,1};
    w->callback(w->arg,nullptr,WEBSOCKET_EVENT_DATA,&d);
    w->callback(w->arg,nullptr,WEBSOCKET_EVENT_CONNECTED,nullptr);
    return ESP_OK;
}
int esp_websocket_client_destroy(FakeWs *w) {delete w;fake::ws=nullptr;--fake::liveWs;return ESP_OK;}
int esp_websocket_client_send_text(FakeWs*,const char *p,int size,unsigned timeout) {
    CHECK(timeout==5000);fake::messages.emplace_back(p,size);return fake::fault=="ws_send"?size-1:size;
}
int esp_websocket_client_send_bin(FakeWs*,const char *p,int size,unsigned timeout) {
    CHECK(timeout==5000);fake::sent.emplace_back(p,p+size);return fake::fault=="ws_send"?size-1:size;
}
FakeMqtt *esp_mqtt_client_init(const esp_mqtt_client_config_t *c) {
    CHECK(c->broker.address.transport==MQTT_TRANSPORT_OVER_SSL);
    CHECK(c->broker.verification.crt_bundle_attach==esp_crt_bundle_attach && c->network.disable_auto_reconnect);
    CHECK(c->network.timeout_ms==5000 && c->session.protocol_ver==MQTT_PROTOCOL_V_3_1_1);
    fake::host=c->broker.address.hostname;fake::port=c->broker.address.port;
    if(fake::fault=="mqtt_init")return nullptr;
    ++fake::liveMqtt;return fake::mqtt=new FakeMqtt;
}
int esp_mqtt_client_register_event(FakeMqtt *m,int event,Handler cb,void *arg) {
    CHECK(event==MQTT_EVENT_ANY);m->callback=cb;m->arg=arg;
    return fake::fault=="mqtt_register"?-1:ESP_OK;
}
int esp_mqtt_client_start(FakeMqtt *m) {
    if(fake::fault=="mqtt_start")return -1;
    if(fake::autoConnect)m->callback(m->arg,nullptr,MQTT_EVENT_CONNECTED,nullptr);
    return ESP_OK;
}
int esp_mqtt_client_stop(FakeMqtt *m) {
    ++fake::mqttStop;esp_mqtt_event_t d{"late",4,4,0};m->callback(m->arg,nullptr,MQTT_EVENT_DATA,&d);
    m->callback(m->arg,nullptr,MQTT_EVENT_CONNECTED,nullptr);return ESP_OK;
}
int esp_mqtt_client_destroy(FakeMqtt *m) {delete m;fake::mqtt=nullptr;--fake::liveMqtt;return ESP_OK;}
int esp_mqtt_client_subscribe(FakeMqtt*,const char*,int qos) {CHECK(qos==0);++fake::subscribes;return fake::fault=="subscribe"?-1:1;}
int esp_mqtt_client_get_outbox_size(FakeMqtt*) {return fake::outbox;}
int esp_mqtt_client_enqueue(FakeMqtt*,const char *topic,const char *p,int size,int qos,int retain,bool store) {
    CHECK(std::string(topic)=="device/test" && qos==0 && !retain && store);
    if(fake::fault=="enqueue")return -1;
    fake::messages.emplace_back(p,size);return 1;
}
int esp_mqtt_client_publish(FakeMqtt*,const char *topic,const char *p,int size,int qos,int retain) {
    CHECK(std::string(topic)=="device/test" && qos==0 && !retain);
    if(fake::fault=="publish")return -1;
    fake::messages.emplace_back(p,size);return 0;
}

int ipaddr_aton(const char *host,ip_addr_t *ip) {
    if(std::string(host)=="192.0.2.1"){ip->value=0x010200c0;ip->ipv4=true;return 1;}
    if(std::string(host)=="::1"){ip->ipv4=false;return 1;}
    return 0;
}
err_t tcpip_callback(void(*cb)(void*),void *arg){if(fake::fault=="tcpip")return -1;cb(arg);return ERR_OK;}
err_t dns_gethostbyname_addrtype(const char*,ip_addr_t *ip,DnsCallback cb,void *arg,int type) {
    CHECK(type==LWIP_DNS_ADDRTYPE_IPV4);
    if(fake::dnsMode==1)return -1;
    if(fake::dnsMode==2){CHECK(!fake::pendingDns);fake::pendingDns=cb;fake::pendingDnsArg=arg;return ERR_INPROGRESS;}
    ip->value=0x010200c0;ip->ipv4=true;return ERR_OK;
}
uint16_t htons(uint16_t v){return uint16_t(v<<8)|uint16_t(v>>8);}
int socket(int family,int type,int protocol) {
    CHECK(family==AF_INET && type==SOCK_DGRAM && protocol==IPPROTO_UDP);
    if(fake::fault=="socket")return -1;
    int fd=fake::nextSocket++;fake::sockets.insert(fd);return fd;
}
int fcntl(int fd,int command,int flags){CHECK(fake::sockets.count(fd) && command==F_SETFL && flags==O_NONBLOCK);return fake::fault=="fcntl"?-1:0;}
int connect(int fd,const sockaddr *addr,size_t size) {
    CHECK(fake::sockets.count(fd) && size==sizeof(sockaddr_in));
    const auto a=reinterpret_cast<const sockaddr_in*>(addr);
    CHECK(a->sin_family==AF_INET && a->sin_port==htons(12345) && a->sin_addr.s_addr==0x010200c0);
    return fake::fault=="socket_connect"?-1:0;
}
int close(int fd){CHECK(!fake::taskActive && fake::sockets.erase(fd)==1);return 0;}
int send(int fd,const void *p,size_t size,int){CHECK(fake::sockets.count(fd));fake::sent.emplace_back(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+size);return fake::fault=="send"?-1:int(size);}
int recv(int fd,void *p,size_t size,int flags) {
    CHECK(fake::sockets.count(fd) && flags==MSG_DONTWAIT);
    if(fake::fault=="recv"){errno=EIO;return -1;}
    if(fake::received.empty()){errno=EWOULDBLOCK;return -1;}
    auto data=std::move(fake::received.front());fake::received.pop_front();
    size=std::min(size,data.size());std::memcpy(p,data.data(),size);return int(size);
}
int psa_crypto_init(){return fake::fault=="crypto_init"?-1:PSA_SUCCESS;}
void psa_set_key_usage_flags(psa_key_attributes_t *p,int v){p->usage=v;}
void psa_set_key_algorithm(psa_key_attributes_t *p,int v){p->alg=v;}
void psa_set_key_type(psa_key_attributes_t *p,int v){p->type=v;}
void psa_set_key_bits(psa_key_attributes_t *p,int v){p->bits=v;}
void psa_reset_key_attributes(psa_key_attributes_t *p){*p={};}
int psa_import_key(const psa_key_attributes_t *a,const uint8_t *data,size_t size,psa_key_id_t *key) {
    CHECK(a->usage==(PSA_KEY_USAGE_ENCRYPT|PSA_KEY_USAGE_DECRYPT) && a->alg==PSA_ALG_CTR && a->type==PSA_KEY_TYPE_AES && a->bits==128 && size==16);
    if(fake::fault=="crypto_import")return -1;
    *key=fake::nextKey++;std::copy_n(data,16,fake::keys[*key].begin());return PSA_SUCCESS;
}
int psa_destroy_key(psa_key_id_t key){CHECK(fake::keys.erase(key)==1);return PSA_SUCCESS;}
int psa_cipher_encrypt_setup(psa_cipher_operation_t *op,psa_key_id_t key,int algorithm) {
    CHECK(fake::keys.count(key) && algorithm==PSA_ALG_CTR);
    if(fake::fault=="crypto_setup")return -1;
    op->key=key;op->active=true;++fake::cryptoLive;return PSA_SUCCESS;
}
int psa_cipher_set_iv(psa_cipher_operation_t *op,const uint8_t *iv,size_t n){CHECK(op->active && n==16);std::copy_n(iv,n,op->iv.begin());return fake::fault=="crypto_iv"?-1:PSA_SUCCESS;}
int psa_cipher_update(psa_cipher_operation_t *op,const uint8_t *p,size_t size,uint8_t *out,size_t capacity,size_t *written) {
    CHECK(op->active && capacity>=size);
    if(fake::fault=="crypto_update")return -1;
    for(size_t i=0;i<size;++i)out[i]=p[i]^fake::keys[op->key][i%16]^op->iv[i%16];
    *written=fake::fault=="crypto_short"?size-1:size;return PSA_SUCCESS;
}
int psa_cipher_finish(psa_cipher_operation_t *op,uint8_t*,size_t,size_t *written){CHECK(op->active);*written=fake::fault=="crypto_tail"?1:0;return fake::fault=="crypto_finish"?-1:PSA_SUCCESS;}
int psa_cipher_abort(psa_cipher_operation_t *op){++fake::abortCalls;if(op->active){--fake::cryptoLive;op->active=false;}return PSA_SUCCESS;}

#include "xiaozhi_transport.inc"

using namespace core::xiaozhi;
CloudConfig config(Transport kind=Transport::WebSocket) {
    CloudConfig c;c.transport=kind;c.wsUrl="wss://example.test/chat";c.token="synthetic-token";
    c.clientId="unit-client";c.deviceId="unit-device";c.mqttEndpoint="example.test";
    c.mqttPublishTopic="device/test";c.mqttClientId="unit-client";return c;
}
ServerEvent hello() {
    ServerEvent h;h.type=EventType::Hello;h.sessionId="session-test";h.udp.server="192.0.2.1";h.udp.port=12345;
    for(size_t i=0;i<16;++i){h.udp.key[i]=uint8_t(i+1);h.udp.nonce[i]=uint8_t(32+i);}
    h.udp.nonce[0]=1;return h;
}
struct Harness {
    std::vector<std::string> text;
    std::vector<std::vector<uint8_t>> audio;
    TransportClient client;
    std::string error;
    Harness():client([this](const char *p,size_t n){text.emplace_back(p,n);},
                     [this](const uint8_t *p,size_t n){audio.emplace_back(p,p+n);}){
        fake::reset();
        fake::taskTick=[this]{
            if(fake::taskActive && client.state_ &&
               (client.stopping_.load() || client.state_->receiverStopping.load()))
                fake::taskEntry(fake::taskArg);
        };
    }
    ~Harness() noexcept(false){client.stop();fake::taskTick={};CHECK(!client.connected());}
    void connect(CloudConfig c=config(),CancelPredicate cancel={}){CHECK(client.connect(c,cancel,error));CHECK(client.connected() && !client.failed() && error.empty());}
    void udp(){connect(config(Transport::MqttUdp));CHECK(client.openAudio(hello(),error));}
    bool pump(){return client.receiveAudio() && client.pollAudio();}
};
void ws(const std::string &data,int opcode=1,bool fin=true,int offset=0,int total=-1) {
    esp_websocket_event_data_t d{data.data(),int(data.size()),total<0?int(data.size()):total,offset,fin,uint8_t(opcode)};
    CHECK(fake::ws);fake::ws->callback(fake::ws->arg,nullptr,WEBSOCKET_EVENT_DATA,&d);
}
void mqtt(const std::string &data,int offset=0,int total=-1) {
    esp_mqtt_event_t d{data.data(),int(data.size()),total<0?int(data.size()):total,offset};
    CHECK(fake::mqtt);fake::mqtt->callback(fake::mqtt->arg,nullptr,MQTT_EVENT_DATA,&d);
}
std::string asString(const std::vector<uint8_t> &v){return {v.begin(),v.end()};}
void set32(std::vector<uint8_t>&v,size_t offset,uint32_t value){for(int i=3;i>=0;--i){v[offset+i]=uint8_t(value);value>>=8;}}

void connectionTests() {
    {Harness h;h.connect();CHECK(fake::headers.find("Authorization: Bearer synthetic-token\r\n")!=std::string::npos);CHECK(fake::headers.find("Protocol-Version: 1\r\nDevice-Id: unit-device\r\nClient-Id: unit-client\r\n")!=std::string::npos);h.client.stop();h.client.stop();CHECK(fake::wsStop==1 && h.text.empty());}
    {Harness h;auto c=config();c.token="Bearer supplied";h.connect(c);CHECK(fake::headers.find("Bearer Bearer")==std::string::npos);}
    {Harness h;h.connect(config(Transport::MqttUdp));CHECK(fake::port==8883 && fake::subscribes==0);h.client.stop();CHECK(fake::mqttStop==1 && h.text.empty());}
    {Harness h;auto c=config(Transport::MqttUdp);c.mqttEndpoint+=":9999";c.mqttSubscribeTopic="test/down";h.connect(c);CHECK(fake::port==9999 && fake::subscribes==1);}
    for(auto kind:{Transport::WebSocket,Transport::MqttUdp}) {
        {Harness h;fake::autoConnect=false;CHECK(!h.client.connect(config(kind),{},h.error));CHECK(fake::now==5000000 && !h.error.empty() && !h.client.state_);}
        {Harness h;fake::autoConnect=false;CHECK(!h.client.connect(config(kind),[]{return fake::now>=40000;},h.error));CHECK(fake::now==40000 && !h.client.state_);}
        {Harness h;CHECK(!h.client.connect(config(kind),[]{return true;},h.error));CHECK(fake::now==0 && !h.client.state_);}
    }
    for(const auto *fault:{"ws_init","ws_register","ws_start","mqtt_init","mqtt_register","mqtt_start","subscribe"}) {
        Harness h;fake::fault=fault;auto c=config(std::string(fault).rfind("ws_",0)==0?Transport::WebSocket:Transport::MqttUdp);c.mqttSubscribeTopic="down";
        CHECK(!h.client.connect(c,{},h.error));CHECK(h.client.failed() && !h.client.state_);
    }
    for(int i=0;i<6;++i){Harness h;auto c=config();if(i==0)c.wsUrl="ws://unsafe";if(i==1)c.protocolVersion=0;if(i==2)c.protocolVersion=4;if(i==3)c.token="bad\r\n";if(i==4)c.deviceId="bad\n";if(i==5)c.clientId=std::string("a\0b",3);CHECK(!h.client.connect(c,{},h.error));CHECK(!fake::ws);}
    for(const auto *endpoint:{"",":8883","host:","host:0","host:65536","host:999999","host:abc","host:12x","user@host","http://host"}){Harness h;auto c=config(Transport::MqttUdp);c.mqttEndpoint=endpoint;CHECK(!h.client.connect(c,{},h.error));CHECK(!fake::mqtt);}
    for(unsigned keepalive:{0U,14U,3601U}){Harness h;auto c=config(Transport::MqttUdp);c.mqttKeepalive=keepalive;CHECK(!h.client.connect(c,{},h.error));}
    for(int event:{WEBSOCKET_EVENT_DISCONNECTED,WEBSOCKET_EVENT_CLOSED,WEBSOCKET_EVENT_ERROR}){Harness h;h.connect();fake::ws->callback(fake::ws->arg,nullptr,event,nullptr);CHECK(h.client.failed() && !h.client.connected());ws("ignored");CHECK(h.text.empty());}
    for(int event:{MQTT_EVENT_DISCONNECTED,MQTT_EVENT_ERROR}){Harness h;h.connect(config(Transport::MqttUdp));fake::mqtt->callback(fake::mqtt->arg,nullptr,event,nullptr);CHECK(h.client.failed() && !h.client.connected());}
}
void receiveTests() {
    const std::string data="{\"type\":\"hello\",\"synthetic\":true}";
    for(size_t split=1;split<data.size();++split){Harness h;h.connect();ws(data.substr(0,split),1,true,0,int(data.size()));CHECK(h.text.empty());ws(data.substr(split),1,true,int(split),int(data.size()));CHECK(!h.client.failed() && h.text==std::vector<std::string>{data});}
    {Harness h;h.connect();ws("part-",1,false);ws("ping",9);ws("two",0,true);CHECK(h.text==std::vector<std::string>{"part-two"});ws("pong",10);CHECK(!h.client.failed());}
    for(int version:{1,2,3})for(size_t count:std::array<size_t,5>{1,2,61,1275,MaxOpusBytes}) {
        Harness h;auto c=config();c.protocolVersion=version;h.connect(c);
        std::vector<uint8_t> opus(count,0x72),packet;CHECK(packWebsocketAudio(version,opus.data(),opus.size(),0x12345678,packet));
        auto data=asString(packet);auto split=data.size()/2;
        if(split){ws(data.substr(0,split),2,false);ws("ping",9);ws(data.substr(split),0,true);}else ws(data,2);
        CHECK(!h.client.failed() && h.audio==std::vector<std::vector<uint8_t>>{opus});
    }
    for(int kind=0;kind<10;++kind) {
        Harness h;h.connect();
        if(kind==0)ws("x",0);
        if(kind==1)ws("x",3);
        if(kind==2)ws("x",1,true,1,2);
        if(kind==3)ws("xx",1,true,0,1);
        if(kind==4)ws("x",1,true,0,int(MaxJsonBytes+1));
        if(kind==5){ws("a",1,false);ws("b",1,true);}
        if(kind==6){ws("a",1,true,0,3);ws("b",1,true,2,3);}
        if(kind==7){ws("a",1,false,0,3);ws("bc",1,true,1,3);}
        if(kind==8){ws(std::string(MaxJsonBytes,'a'),1,false);ws("b",0,true);}
        if(kind==9)ws(std::string(MaxWireAudio+1,'x'),2);
        CHECK(h.client.failed() && h.text.empty() && h.audio.empty());
    }
    {Harness h;h.connect();esp_websocket_event_data_t d{nullptr,1,1,0,true,1};fake::ws->callback(fake::ws->arg,nullptr,WEBSOCKET_EVENT_DATA,&d);CHECK(h.client.failed());}
    {Harness h;h.connect();h.client.onText_=[](const char*,size_t){throw std::bad_alloc();};ws("x");CHECK(h.client.failed());}
    {Harness h;h.connect();h.client.onAudio_=[](const uint8_t*,size_t){throw std::runtime_error("synthetic");};ws("x",2);CHECK(h.client.failed());}
    for(size_t split=1;split<data.size();++split){Harness h;h.connect(config(Transport::MqttUdp));mqtt(data.substr(0,split),0,int(data.size()));CHECK(h.text.empty());mqtt(data.substr(split),int(split),int(data.size()));CHECK(!h.client.failed() && h.text==std::vector<std::string>{data});}
    {Harness h;h.connect(config(Transport::MqttUdp));mqtt(std::string(MaxJsonBytes,'x'));CHECK(h.text.size()==1 && h.text.front().size()==MaxJsonBytes);}
    for(int kind=0;kind<7;++kind){Harness h;h.connect(config(Transport::MqttUdp));if(kind==0)mqtt("");if(kind==1)mqtt("x",0,int(MaxJsonBytes+1));if(kind==2)mqtt("x",1,2);if(kind==3)mqtt("xx",0,1);if(kind==4){mqtt("a",0,3);mqtt("bc",0,3);}if(kind==5){mqtt("a",0,3);mqtt("c",2,3);}if(kind==6){mqtt("a",0,3);mqtt("bc",1,4);}CHECK(h.client.failed() && h.text.empty());}
}
void sendTests() {
    for(int version:{1,2,3}){Harness h;auto c=config();c.protocolVersion=version;h.connect(c);auto capacity=h.client.state_->outgoing.capacity();std::vector<uint8_t> data(MaxOpusBytes,0x39);for(unsigned i=0;i<20;++i){CHECK(h.client.sendAudio(data.data(),data.size(),i*60));CHECK(h.client.state_->outgoing.capacity()==capacity);const uint8_t *view=nullptr;size_t size=0;uint32_t stamp=0;const auto &packet=fake::sent.back();CHECK(unpackWebsocketAudio(version,packet.data(),packet.size(),view,size,stamp));CHECK(size==data.size() && !std::memcmp(view,data.data(),size));if(version==2)CHECK(stamp==i*60);}CHECK(h.client.sendText("{}"));CHECK(!h.client.sendText("") && !h.client.sendText(std::string(MaxJsonBytes+1,'x')) && !h.client.sendAudio(nullptr,1,0) && !h.client.sendAudio(data.data(),MaxOpusBytes+1,0) && !h.client.failed());}
    {Harness h;h.connect();fake::fault="ws_send";CHECK(!h.client.sendText("{}") && h.client.failed());}
    {Harness h;h.connect();fake::fault="ws_send";const uint8_t data=1;CHECK(!h.client.sendAudio(&data,1,0) && h.client.failed());}
    {Harness h;h.connect(config(Transport::MqttUdp));fake::outbox=32766;CHECK(h.client.sendText("{}"));CHECK(fake::messages.size()==1);}
    {Harness h;h.connect(config(Transport::MqttUdp));fake::fault="publish";CHECK(!h.client.sendText("{}") && h.client.failed());}
}
void udpTests() {
    // Run production RX and TX concurrently: their wire buffers, sequencing
    // and cipher operations must remain independent. Synthetic packets are
    // prequeued so the fake queue itself needs no cross-thread mutation.
    {
        Harness h;h.udp();constexpr unsigned count=256;
        std::vector<std::vector<uint8_t>> expected;
        for(unsigned i=0;i<count;++i){std::vector<uint8_t> payload(128,uint8_t(i));expected.push_back(payload);CHECK(h.client.sendAudio(payload.data(),payload.size(),i*60));fake::received.push_back(fake::sent.back());}
        fake::sent.clear();std::atomic<bool> go{false};
        std::thread receiver([&]{while(!go.load())std::this_thread::yield();for(unsigned i=0;i<count/8;++i)CHECK(h.client.receiveAudio());});
        go=true;
        for(unsigned i=0;i<count;++i){const std::array<uint8_t,3> payload{0x65,uint8_t(i),0x76};CHECK(h.client.sendAudio(payload.data(),payload.size(),i*60));}
        receiver.join();CHECK(h.audio==expected && fake::sent.size()==count && fake::received.empty());
        for(unsigned i=0;i<count;++i)CHECK(read32(fake::sent[i].data()+12)==count+i+1);
        CHECK(h.client.state_->receivedSequence==count && h.client.state_->sentSequence==2*count && fake::cryptoLive==0);
    }
    {Harness h;h.udp();const uint8_t first=0x64;CHECK(h.client.sendAudio(&first,1,0));fake::received.push_back(fake::sent.back());h.client.onAudio_=[&](const uint8_t *p,size_t n){const uint8_t second=0x77;CHECK(h.client.sendAudio(&second,1,60));h.audio.emplace_back(p,p+n);};CHECK(h.pump());CHECK(h.audio.size()==1 && h.audio[0]==std::vector<uint8_t>{first});CHECK(fake::sent.size()==2);}
    {Harness h;h.udp();const auto original=hello();for(size_t count:std::array<size_t,3>{1,1275,MaxOpusBytes}){std::vector<uint8_t> data(count,0x5a);CHECK(h.client.sendAudio(data.data(),count,0x12345678));auto packet=fake::sent.back();CHECK(packet.size()==count+16 && packet[0]==1 && packet[1]==original.udp.nonce[1] && packet[2]==uint8_t(count>>8) && packet[3]==uint8_t(count));CHECK(read32(packet.data()+8)==0x12345678);CHECK(read32(packet.data()+12)==fake::sent.size());CHECK(std::equal(packet.begin()+4,packet.begin()+8,original.udp.nonce.begin()+4));fake::received.push_back(packet);CHECK(h.pump());CHECK(h.audio.back()==data);}CHECK(fake::abortCalls==6);h.client.stop();CHECK(fake::messages.size()==1 && fake::messages[0]==makeGoodbye("session-test"));h.client.stop();CHECK(fake::messages.size()==1 && h.text.empty());}
    {Harness h;bool cancel=false;h.connect(config(Transport::MqttUdp),[&]{return cancel;});CHECK(h.client.openAudio(hello(),h.error));cancel=true;h.client.stop();CHECK(fake::messages.size()==1 && fake::mqttStop==1 && fake::now==25000);}
    {Harness h;h.udp();fake::outbox=32768;h.client.stop();CHECK(fake::messages.empty() && fake::mqttStop==1);}
    {Harness h;h.udp();const uint8_t value=7;for(unsigned i=0;i<10;++i){CHECK(h.client.sendAudio(&value,1,i));fake::received.push_back(fake::sent.back());}CHECK(h.pump() && h.audio.size()==8 && fake::received.size()==2);CHECK(h.pump() && h.audio.size()==10);fake::received.push_back(fake::sent.front());CHECK(h.pump() && h.audio.size()==10);}
    {Harness h;h.udp();const uint8_t value=1;CHECK(h.client.sendAudio(&value,1,0));const auto packet=fake::sent.back();for(int kind=0;kind<8;++kind){auto bad=packet;if(kind==0)bad.resize(0);if(kind==1)bad.resize(15);if(kind==2)bad[0]=9;if(kind==3)bad[3]=2;if(kind==4)bad[3]=0;if(kind==5)bad.resize(MaxWireAudio+100,0);if(kind==6)set32(bad,12,0);if(kind==7)bad.push_back(0);fake::received.push_back(std::move(bad));}CHECK(h.pump() && h.audio.empty());fake::received.push_back(packet);CHECK(h.pump() && h.audio.size()==1);fake::received.push_back(packet);CHECK(h.pump() && h.audio.size()==1);}
    {Harness h;h.udp();const uint8_t value=9;h.client.state_->sentSequence=UINT32_MAX-1;CHECK(h.client.sendAudio(&value,1,0));CHECK(read32(fake::sent.back().data()+12)==UINT32_MAX);CHECK(!h.client.sendAudio(&value,1,0) && h.client.failed() && fake::sent.size()==1);}
    for(const char *fault:{"crypto_setup","crypto_iv","crypto_update","crypto_short","crypto_tail","crypto_finish","send"}){Harness h;h.udp();const uint8_t value=1;fake::fault=fault;CHECK(!h.client.sendAudio(&value,1,0) && h.client.failed());CHECK(fake::abortCalls==1 && fake::cryptoLive==0);}
    {Harness h;h.udp();const uint8_t value=1;CHECK(h.client.sendAudio(&value,1,0));fake::received.push_back(fake::sent.back());fake::fault="crypto_update";CHECK(!h.pump() && h.client.failed() && h.audio.empty());}
    {Harness h;h.udp();fake::fault="recv";CHECK(!h.pump() && h.client.failed());}
    for(const char *fault:{"crypto_init","crypto_import","socket","fcntl","socket_connect","task_create"}){Harness h;h.connect(config(Transport::MqttUdp));fake::fault=fault;CHECK(!h.client.openAudio(hello(),h.error) && h.client.failed() && !h.error.empty());h.client.stop();CHECK(fake::keys.empty() && fake::sockets.empty());}
    for(int kind=0;kind<5;++kind){Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();if(kind==0)event.type=EventType::Stt;if(kind==1)event.udp.server="";if(kind==2)event.udp.server="bad\n";if(kind==3)event.udp.port=0;if(kind==4)event.udp.nonce[0]=0;CHECK(!h.client.openAudio(event,h.error) && h.client.failed());}
    {Harness h;h.udp();auto previous=h.client.state_->key;CHECK(h.client.openAudio(hello(),h.error));CHECK(fake::keys.size()==1 && !fake::keys.count(previous) && fake::sockets.size()==1);}
    {Harness h;h.connect();CHECK(h.client.openAudio(hello(),h.error) && fake::sockets.empty());CHECK(h.pump());}
}
void dnsTests() {
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="voice.example.test";CHECK(h.client.openAudio(event,h.error));CHECK(fake::now==0);}
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="voice.example.test";fake::dnsMode=2;fake::delayHook=[]{if(fake::now>=60000 && fake::pendingDns)fake::dnsFinish();};CHECK(h.client.openAudio(event,h.error));CHECK(fake::now==60000 && !fake::pendingDns);fake::delayHook={};}
    {Harness h;h.connect(config(Transport::MqttUdp),[]{return fake::now>=40000;});auto event=hello();event.udp.server="voice.example.test";fake::dnsMode=2;CHECK(!h.client.openAudio(event,h.error));CHECK(fake::now==40000 && fake::pendingDns);h.client.stop();CHECK(!h.client.state_);fake::dnsFinish();CHECK(!fake::pendingDns && h.text.empty());}
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="voice.example.test";fake::dnsMode=2;CHECK(!h.client.openAudio(event,h.error));CHECK(fake::now==5000000 && fake::pendingDns);h.client.stop();fake::dnsFinish(false);CHECK(!fake::pendingDns);}
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="voice.example.test";fake::dnsMode=1;CHECK(!h.client.openAudio(event,h.error));CHECK(fake::sockets.empty() && fake::keys.empty());}
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="voice.example.test";fake::fault="tcpip";CHECK(!h.client.openAudio(event,h.error));CHECK(!fake::pendingDns);}
    {Harness h;h.connect(config(Transport::MqttUdp));auto event=hello();event.udp.server="::1";CHECK(!h.client.openAudio(event,h.error));}
}
int main() {
    try {
        connectionTests();receiveTests();sendTests();udpTests();dnsTests();
        CHECK(fake::liveWs==0 && fake::liveMqtt==0 && fake::keys.empty() && fake::sockets.empty() && !fake::pendingDns && fake::cryptoLive==0);
        std::cout<<"{\"passed\":true,\"scenarios\":"<<scenarios<<",\"checks\":"<<checks<<"}\n";
        return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
