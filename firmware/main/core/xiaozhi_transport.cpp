// Official Xiaozhi wire formats; upstream attribution: xiaozhi.LICENSE.
#include "xiaozhi_transport.hpp"
#include "esp_websocket_client.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "psa/crypto.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <unistd.h>

namespace core::xiaozhi {
namespace {
constexpr unsigned TimeoutMs=5000;
constexpr TickType_t ReceiverTick=std::max<TickType_t>(1,pdMS_TO_TICKS(5));
constexpr size_t UdpHeader=16, MaxWireAudio=MaxOpusBytes+16;
bool canEnqueue(int pending,size_t size) {
    return pending>=0 && pending<=32768 && size<=32768-size_t(pending);
}
bool cleanHeader(const std::string &value) {
    return std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c!=127;});
}
uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
void write32(uint8_t *p,uint32_t value) {
    p[0]=uint8_t(value>>24);p[1]=uint8_t(value>>16);p[2]=uint8_t(value>>8);p[3]=uint8_t(value);
}
bool crypt(psa_key_id_t key,const uint8_t *iv,const uint8_t *input,size_t size,uint8_t *out) {
    psa_cipher_operation_t operation=PSA_CIPHER_OPERATION_INIT;
    auto status=psa_cipher_encrypt_setup(&operation,key,PSA_ALG_CTR);
    if(status==PSA_SUCCESS)status=psa_cipher_set_iv(&operation,iv,16);
    size_t written=0,tail=0;
    if(status==PSA_SUCCESS)status=psa_cipher_update(&operation,input,size,out,size,&written);
    uint8_t scratch[16]{};
    if(status==PSA_SUCCESS)status=psa_cipher_finish(&operation,scratch,sizeof(scratch),&tail);
    (void)psa_cipher_abort(&operation);
    return status==PSA_SUCCESS&&written==size&&!tail;
}

// DNS executes on lwIP's thread. A pending request owns only this shared state,
// never a TransportClient. Cancellation may release the worker immediately;
// the normal lwIP callback eventually releases its own remaining reference.
struct DnsRequest {
    std::string host;
    ip_addr_t address{};
    std::atomic<bool> done{false};
    bool ok=false;
};
using DnsHolder=std::shared_ptr<DnsRequest>;
void dnsDone(const char *,const ip_addr_t *address,void *arg) {
    std::unique_ptr<DnsHolder> holder(static_cast<DnsHolder *>(arg));
    auto &request=**holder;
    request.ok=address&&IP_IS_V4(address);
    if(request.ok)request.address=*address;
    request.done.store(true,std::memory_order_release);
}
void dnsBegin(void *arg) {
    auto holder=static_cast<DnsHolder *>(arg);
    auto &request=**holder;
    ip_addr_t address{};
    const auto result=dns_gethostbyname_addrtype(request.host.c_str(),&address,dnsDone,holder,LWIP_DNS_ADDRTYPE_IPV4);
    if(result!=ERR_INPROGRESS)dnsDone(nullptr,result==ERR_OK?&address:nullptr,holder);
}
bool resolve(const std::string &host,const CancelPredicate &cancel,ip_addr_t &address) {
    if(ipaddr_aton(host.c_str(),&address))return IP_IS_V4_VAL(address);
    auto request=std::make_shared<DnsRequest>();
    request->host=host;
    auto holder=new(std::nothrow) DnsHolder(request);
    if(!holder)return false;
    if(tcpip_callback(dnsBegin,holder)!=ERR_OK) {delete holder;return false;}
    const int64_t deadline=esp_timer_get_time()+int64_t(TimeoutMs)*1000;
    while(!request->done.load(std::memory_order_acquire)) {
        if((cancel&&cancel()) || esp_timer_get_time()>=deadline)return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if(!request->ok || (cancel&&cancel()))return false;
    address=request->address;return true;
}
}

struct TransportClient::State {
    CloudConfig config;
    CancelPredicate cancel;
    esp_websocket_client_handle_t websocket=nullptr;
    esp_mqtt_client_handle_t mqtt=nullptr;
    bool wsStarted=false,mqttStarted=false;
    std::string headers,mqttHost,goodbye;
    std::vector<uint8_t> outgoing;
    std::array<uint8_t,MaxJsonBytes> incoming{};
    size_t received=0,frameLength=0,frameUsed=0;
    int opcode=0;
    bool frameActive=false,messageOpen=false,frameFinal=false;
    int udp=-1;
    psa_key_id_t key=PSA_KEY_ID_NULL;
    std::array<uint8_t,16> nonce{};
    uint32_t sentSequence=0,receivedSequence=0;
    std::array<uint8_t,MaxWireAudio+1> wire{};
    std::array<uint8_t,MaxWireAudio+1> incomingAudio{};
    std::array<uint8_t,MaxOpusBytes> plain{};
    std::mutex cryptoMutex;
    std::atomic<bool> receiverRunning{false},receiverStopping{false};
};

TransportClient::TransportClient(TextCallback onText,AudioCallback onAudio)
    :onText_(std::move(onText)),onAudio_(std::move(onAudio)) {}
TransportClient::~TransportClient() {stop();}
void TransportClient::fail(const char *message) {
    const char *expected=nullptr;
    failure_.compare_exchange_strong(expected,message);
    connected_=false;
}
std::string TransportClient::error() const {
    auto value=failure_.load();return value?value:"";
}
bool TransportClient::cancelled() const {
    return stopping_.load() || (state_ && state_->cancel && state_->cancel());
}
bool TransportClient::deliverText(const uint8_t *data,size_t size) {
    if(stopping_.load()||failed())return false;
    try {if(onText_)onText_(reinterpret_cast<const char *>(data),size);return true;}
    catch(const std::bad_alloc &){fail("小智接收内存不足");return false;}
    catch(...){fail("小智消息处理失败");return false;}
}
bool TransportClient::deliverAudio(const uint8_t *data,size_t size) {
    if(stopping_.load()||failed())return false;
    try {if(onAudio_)onAudio_(data,size);return true;}
    catch(const std::bad_alloc &){fail("小智音频缓冲不足");return false;}
    catch(...){fail("小智音频处理失败");return false;}
}

bool TransportClient::connect(const CloudConfig &config,const CancelPredicate &cancel,std::string &message) {
    stop();stopping_=false;failure_=nullptr;message.clear();
    try {
        state_=new(std::nothrow) State;
        if(!state_) {fail("小智连接内存不足");message=error();return false;}
        auto &s=*state_;
        s.config=config;s.cancel=cancel;
        if(cancelled()) {fail("连接已取消");message=error();stop();return false;}
        if(config.transport==Transport::WebSocket) {
            s.outgoing.reserve(MaxWireAudio);
            if(config.wsUrl.rfind("wss://",0)!=0 || config.protocolVersion<1 || config.protocolVersion>3 ||
               !cleanHeader(config.wsUrl) || !cleanHeader(config.token) || !cleanHeader(config.deviceId) ||
               !cleanHeader(config.clientId)) {fail("小智安全连接配置无效");}
            if(!config.token.empty())s.headers=std::string("Authorization: ")+
                (config.token.find(' ')==std::string::npos?"Bearer ":"")+config.token+"\r\n";
            s.headers+="Protocol-Version: "+std::to_string(config.protocolVersion)+"\r\nDevice-Id: "+
                       config.deviceId+"\r\nClient-Id: "+config.clientId+"\r\n";
            esp_websocket_client_config_t ws{};
            ws.uri=s.config.wsUrl.c_str();ws.headers=s.headers.c_str();
            ws.crt_bundle_attach=esp_crt_bundle_attach;
            ws.disable_auto_reconnect=true;
            ws.network_timeout_ms=TimeoutMs;
            ws.reconnect_timeout_ms=TimeoutMs;
            ws.buffer_size=2048;ws.task_stack=6144;ws.task_prio=4;
            if(!failed())s.websocket=esp_websocket_client_init(&ws);
            if(!s.websocket || esp_websocket_register_events(s.websocket,WEBSOCKET_EVENT_ANY,
                   websocketCallback,this)!=ESP_OK || esp_websocket_client_start(s.websocket)!=ESP_OK)
                fail("小智连接启动失败");
            else s.wsStarted=true;
        } else {
            s.mqttHost=config.mqttEndpoint;
            unsigned port=8883;
            auto colon=s.mqttHost.find(':');
            if(colon!=std::string::npos) {
                auto digits=s.mqttHost.substr(colon+1);s.mqttHost.resize(colon);port=0;
                if(digits.empty()||digits.size()>5)fail("小智消息地址无效");
                for(char c:digits) {if(c<'0'||c>'9'){fail("小智消息地址无效");break;}port=port*10+unsigned(c-'0');}
            }
            if(s.mqttHost.empty() || !cleanHeader(s.mqttHost) || s.mqttHost.find_first_of(" /@?#\\")!=std::string::npos ||
               !port || port>65535 || config.mqttPublishTopic.empty() || config.mqttKeepalive<15 || config.mqttKeepalive>3600)
                fail("小智消息配置无效");
            esp_mqtt_client_config_t mqtt{};
            mqtt.broker.address.hostname=s.mqttHost.c_str();mqtt.broker.address.port=port;
            mqtt.broker.address.transport=MQTT_TRANSPORT_OVER_SSL;
            mqtt.broker.verification.crt_bundle_attach=esp_crt_bundle_attach;
            mqtt.credentials.client_id=s.config.mqttClientId.c_str();
            mqtt.credentials.username=s.config.mqttUsername.c_str();
            mqtt.credentials.authentication.password=s.config.mqttPassword.c_str();
            mqtt.session.keepalive=config.mqttKeepalive;
            mqtt.session.protocol_ver=MQTT_PROTOCOL_V_3_1_1;
            mqtt.network.disable_auto_reconnect=true;mqtt.network.timeout_ms=TimeoutMs;
            mqtt.task.stack_size=6144;mqtt.task.priority=4;
            mqtt.buffer.size=2048;mqtt.buffer.out_size=2048;
            if(!failed())s.mqtt=esp_mqtt_client_init(&mqtt);
            if(!s.mqtt || esp_mqtt_client_register_event(s.mqtt,MQTT_EVENT_ANY,mqttCallback,this)!=ESP_OK ||
               esp_mqtt_client_start(s.mqtt)!=ESP_OK)fail("小智消息连接启动失败");
            else s.mqttStarted=true;
        }
        const int64_t deadline=esp_timer_get_time()+int64_t(TimeoutMs)*1000;
        while(!connected()&&!failed()) {
            if(cancelled()) {fail("连接已取消");break;}
            if(esp_timer_get_time()>=deadline){fail("小智连接超时，请重试");break;}
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if(connected() && !s.config.mqttSubscribeTopic.empty() && s.mqtt &&
           esp_mqtt_client_subscribe(s.mqtt,s.config.mqttSubscribeTopic.c_str(),0)<0)
            fail("小智消息订阅失败");
        if(cancelled())fail("连接已取消");
        if(failed()) {message=error();stop();return false;}
        return true;
    } catch(const std::bad_alloc &) {
        fail("小智连接内存不足");message=error();stop();return false;
    }
}

void TransportClient::websocketCallback(void *arg,const char *,int32_t event,void *data) {
    static_cast<TransportClient *>(arg)->websocketEvent(event,data);
}
void TransportClient::mqttCallback(void *arg,const char *,int32_t event,void *data) {
    static_cast<TransportClient *>(arg)->mqttEvent(event,data);
}
void TransportClient::websocketEvent(int32_t event,void *raw) {
    if(stopping_.load()||!state_)return;
    if(event==WEBSOCKET_EVENT_CONNECTED) {if(!failed())connected_=true;return;}
    if(event==WEBSOCKET_EVENT_DISCONNECTED || event==WEBSOCKET_EVENT_CLOSED) {fail("小智连接已断开");return;}
    if(event==WEBSOCKET_EVENT_ERROR) {fail("小智安全连接失败");return;}
    if(event!=WEBSOCKET_EVENT_DATA || failed() || !raw)return;
    auto &s=*state_;
    const auto &d=*static_cast<esp_websocket_event_data_t *>(raw);
    if(d.op_code==8 || d.op_code==9 || d.op_code==10)return;
    if(d.data_len<0 || d.payload_len<0 || d.payload_offset<0 ||
       (d.data_len&&!d.data_ptr)) {fail("小智响应数据无效");return;}
    const auto total=size_t(d.payload_len),offset=size_t(d.payload_offset),size=size_t(d.data_len);
    if(!offset) {
        if(s.frameActive) {fail("小智响应分片不连续");return;}
        if(d.op_code==1 || d.op_code==2) {
            if(s.messageOpen) {fail("小智响应分片不连续");return;}
            s.messageOpen=true;s.opcode=d.op_code;s.received=0;
        } else if(d.op_code!=0 || !s.messageOpen) {fail("小智响应分片无效");return;}
        s.frameActive=true;s.frameLength=total;s.frameUsed=0;s.frameFinal=d.fin;
    }
    const size_t limit=s.opcode==1?MaxJsonBytes:MaxWireAudio;
    if(!s.frameActive || total!=s.frameLength || offset!=s.frameUsed || bool(d.fin)!=s.frameFinal ||
       offset>total || size>total-offset || total>limit || s.received>limit-size) {
        fail("小智响应超限或分片缺失");return;
    }
    if(size)std::memcpy(s.incoming.data()+s.received,d.data_ptr,size);
    s.received+=size;s.frameUsed+=size;
    if(s.frameUsed==s.frameLength) {
        s.frameActive=false;
        if(s.frameFinal) {
            if(s.opcode==1)deliverText(s.incoming.data(),s.received);
            else {
                const uint8_t *audio=nullptr;size_t bytes=0;uint32_t timestamp=0;
                if(!unpackWebsocketAudio(s.config.protocolVersion,s.incoming.data(),s.received,audio,bytes,timestamp))
                    fail("小智音频数据无效");
                else deliverAudio(audio,bytes);
            }
            s.messageOpen=false;s.received=0;s.opcode=0;
        }
    }
}
void TransportClient::mqttEvent(int32_t event,void *raw) {
    if(stopping_.load()||!state_)return;
    if(event==MQTT_EVENT_CONNECTED) {if(!failed())connected_=true;return;}
    if(event==MQTT_EVENT_DISCONNECTED) {fail("小智消息连接已断开");return;}
    if(event==MQTT_EVENT_ERROR) {fail("小智消息安全连接失败");return;}
    if(event!=MQTT_EVENT_DATA || failed() || !raw)return;
    auto &s=*state_;
    const auto &d=*static_cast<esp_mqtt_event_t *>(raw);
    if(d.total_data_len<=0 || d.current_data_offset<0 || d.data_len<0 || (d.data_len&&!d.data)) {
        fail("小智消息数据无效");return;
    }
    const size_t total=size_t(d.total_data_len),offset=size_t(d.current_data_offset),size=size_t(d.data_len);
    if(!offset) {
        if(s.messageOpen){fail("小智消息分片不连续");return;}
        s.messageOpen=true;s.received=0;s.frameLength=total;
    }
    if(!s.messageOpen || total>MaxJsonBytes || total!=s.frameLength || offset!=s.received ||
       offset>total || size>total-offset) {fail("小智消息超限或分片缺失");return;}
    if(size)std::memcpy(s.incoming.data()+offset,d.data,size);
    s.received+=size;
    if(s.received==total) {deliverText(s.incoming.data(),s.received);s.messageOpen=false;s.received=0;}
}

bool TransportClient::sendText(const std::string &text) {
    if(!state_ || !connected() || cancelled() || text.empty() || text.size()>MaxJsonBytes)return false;
    auto &s=*state_;
    if(s.websocket) {
        if(esp_websocket_client_send_text(s.websocket,text.data(),int(text.size()),pdMS_TO_TICKS(TimeoutMs))==int(text.size()))return true;
    // listen/start must reach the control socket before the worker can send
    // the first UDP audio packet. Enqueue alone provides no such boundary.
    } else if(s.mqtt && esp_mqtt_client_publish(s.mqtt,s.config.mqttPublishTopic.c_str(),
                                              text.data(),int(text.size()),0,0)>=0)
        return true;
    fail("小智消息发送失败");return false;
}
bool TransportClient::sendAudio(const uint8_t *data,size_t size,uint32_t timestamp) {
    if(!state_ || !connected() || cancelled() || !data || !size || size>MaxOpusBytes)return false;
    auto &s=*state_;
    if(s.websocket) {
        try {
            if(packWebsocketAudio(s.config.protocolVersion,data,size,timestamp,s.outgoing) &&
               esp_websocket_client_send_bin(s.websocket,reinterpret_cast<const char *>(s.outgoing.data()),
                   int(s.outgoing.size()),pdMS_TO_TICKS(TimeoutMs))==int(s.outgoing.size()))return true;
        } catch(const std::bad_alloc &) {fail("小智发送内存不足");return false;}
    } else if(s.udp>=0 && s.key!=PSA_KEY_ID_NULL) {
        if(s.sentSequence==UINT32_MAX){fail("小智语音会话已结束，请重新连接");return false;}
        std::copy(s.nonce.begin(),s.nonce.end(),s.wire.begin());
        s.wire[2]=uint8_t(size>>8);s.wire[3]=uint8_t(size);
        write32(s.wire.data()+8,timestamp);write32(s.wire.data()+12,++s.sentSequence);
        bool encrypted=false;
        {std::lock_guard<std::mutex> lock(s.cryptoMutex);
         encrypted=crypt(s.key,s.wire.data(),data,size,s.wire.data()+UdpHeader);}
        if(encrypted &&
           ::send(s.udp,s.wire.data(),size+UdpHeader,0)==int(size+UdpHeader))return true;
    }
    fail("小智语音发送失败");return false;
}

bool TransportClient::openAudio(const ServerEvent &hello,std::string &message) {
    message.clear();
    if(!state_ || !connected() || cancelled()) {message="小智连接已结束";return false;}
    if(state_->websocket)return true;
    auto &s=*state_;
    stopAudioReceiver();
    if(s.udp>=0){::close(s.udp);s.udp=-1;}
    if(s.key!=PSA_KEY_ID_NULL){(void)psa_destroy_key(s.key);s.key=PSA_KEY_ID_NULL;}
    try {
        if(hello.type!=EventType::Hello || hello.udp.server.empty() || hello.udp.server.size()>253 ||
           !cleanHeader(hello.udp.server) || !hello.udp.port || hello.udp.nonce[0]!=1) {
            fail("小智语音通道配置无效");message=error();return false;
        }
        ip_addr_t ip{};
        if(!resolve(hello.udp.server,[this]{return cancelled();},ip)) {
            fail(cancelled()?"连接已取消":"小智语音地址解析失败或超时");message=error();return false;
        }
        if(cancelled()) {fail("连接已取消");message=error();return false;}
        if(psa_crypto_init()!=PSA_SUCCESS){fail("语音加密初始化失败");message=error();return false;}
        psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_ENCRYPT|PSA_KEY_USAGE_DECRYPT);
        psa_set_key_algorithm(&attributes,PSA_ALG_CTR);
        psa_set_key_type(&attributes,PSA_KEY_TYPE_AES);psa_set_key_bits(&attributes,128);
        const auto imported=psa_import_key(&attributes,hello.udp.key.data(),hello.udp.key.size(),&s.key);
        psa_reset_key_attributes(&attributes);
        if(imported!=PSA_SUCCESS){fail("语音密钥初始化失败");message=error();return false;}
        s.udp=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        sockaddr_in address{};
        address.sin_family=AF_INET;address.sin_port=htons(hello.udp.port);
        address.sin_addr.s_addr=ip4_addr_get_u32(ip_2_ip4(&ip));
        if(s.udp<0 || ::fcntl(s.udp,F_SETFL,O_NONBLOCK)<0 ||
           ::connect(s.udp,reinterpret_cast<sockaddr *>(&address),sizeof(address))<0) {
            fail("小智语音通道连接失败");message=error();return false;
        }
        s.goodbye=makeGoodbye(hello.sessionId);
        s.nonce=hello.udp.nonce;s.sentSequence=s.receivedSequence=0;
        s.receiverStopping=false;s.receiverRunning=true;
        if(xTaskCreate(audioReceiver,"xiaozhi_udp",4096,this,4,nullptr)!=pdPASS) {
            s.receiverRunning=false;fail("小智语音接收启动失败");message=error();return false;
        }
        return true;
    } catch(const std::bad_alloc &) {fail("小智语音连接内存不足");message=error();return false;}
}

bool TransportClient::pollAudio() {
    if(!state_ || failed() || cancelled())return false;
    return state_->udp<0 || state_->receiverRunning.load();
}
bool TransportClient::receiveAudio() {
    if(!state_ || failed() || cancelled())return false;
    auto &s=*state_;
    if(s.udp<0)return true;
    auto &wire=s.incomingAudio;
    for(unsigned packet=0;packet<8;++packet) {
        if(s.receiverStopping.load() || cancelled())return false;
        const int bytes=::recv(s.udp,wire.data(),wire.size(),MSG_DONTWAIT);
        if(bytes<0) {
            if(errno==EWOULDBLOCK || errno==EAGAIN || errno==EINTR)return true;
            fail("小智语音接收失败");return false;
        }
        if(bytes<int(UdpHeader) || bytes>int(MaxWireAudio) || wire[0]!=1)continue;
        const size_t size=size_t(wire[2])<<8 | wire[3];
        const uint32_t sequence=read32(wire.data()+12);
        if(!size || size>MaxOpusBytes || size+UdpHeader!=size_t(bytes) || sequence<=s.receivedSequence)continue;
        bool decrypted=false;
        {std::lock_guard<std::mutex> lock(s.cryptoMutex);
         decrypted=crypt(s.key,wire.data(),wire.data()+UdpHeader,size,s.plain.data());}
        if(!decrypted) {
            fail("小智语音解码失败");return false;
        }
        s.receivedSequence=sequence;
        if(!deliverAudio(s.plain.data(),size))return false;
    }
    return true;
}
void TransportClient::audioReceiver(void *arg) {
    auto client=static_cast<TransportClient *>(arg);
    auto state=client->state_;
    while(!state->receiverStopping.load() && client->receiveAudio())
        vTaskDelay(ReceiverTick);
    state->receiverRunning.store(false);
    // No State or client access beyond this point. stopAudioReceiver may now
    // release them; FreeRTOS reclaims the deleted task separately.
    vTaskDelete(nullptr);
}
void TransportClient::stopAudioReceiver() {
    if(!state_)return;
    state_->receiverStopping=true;
    while(state_->receiverRunning.load())vTaskDelay(ReceiverTick);
}

void TransportClient::stop() {
    stopping_=true;
    const bool wasConnected=connected_.exchange(false);
    if(!state_)return;
    // SDK stop/destroy joins its event task. Keep State and callbacks alive
    // until that fence completes; callback-side methods observe stopping_.
    auto s=state_;
    stopAudioReceiver();
    // Queue at most once, without going through cancelled(). A cancelled app
    // still releases the broker's audio session; no synchronous publish or
    // reconnect is attempted. Delivery is best-effort before SDK shutdown.
    if(wasConnected && s->mqtt && s->mqttStarted && !s->goodbye.empty() &&
       canEnqueue(esp_mqtt_client_get_outbox_size(s->mqtt),s->goodbye.size())) {
        if(esp_mqtt_client_enqueue(s->mqtt,s->config.mqttPublishTopic.c_str(),
                s->goodbye.data(),int(s->goodbye.size()),0,0,true)>=0)
            vTaskDelay(pdMS_TO_TICKS(20));
        s->goodbye.clear();
    }
    if(s->websocket) {
        if(s->wsStarted)(void)esp_websocket_client_stop(s->websocket);
        (void)esp_websocket_client_destroy(s->websocket);
    }
    if(s->mqtt) {
        if(s->mqttStarted)(void)esp_mqtt_client_stop(s->mqtt);
        (void)esp_mqtt_client_destroy(s->mqtt);
    }
    if(s->udp>=0)::close(s->udp);
    if(s->key!=PSA_KEY_ID_NULL)(void)psa_destroy_key(s->key);
    std::fill(s->nonce.begin(),s->nonce.end(),uint8_t(0));
    connected_=false;state_=nullptr;delete s;
}
} // namespace core::xiaozhi
