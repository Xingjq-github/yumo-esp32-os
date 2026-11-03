// Runs the production service state machine and production JSON parser. Only
// transport, audio, global services, allocation and task/time APIs are doubles.
#include "core/xiaozhi_service.hpp"
#include "core/xiaozhi_transport.hpp"
#include "core/xiaozhi_audio.hpp"
#include "core/power_mgr.hpp"
#include "host_services.hpp"
#include "cJSON.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace core::xiaozhi;
namespace host {
unsigned checks=0,scenarios=0;
const char *scenario="initial";
void check(bool value,const char *message) {
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL [%s]: %s\n",scenario,message);std::exit(1);}
}
struct Event {std::string kind;int64_t at;int value=0;};
struct World {
    int64_t clock=1000000;
    bool screen=true,sleep=false,wifi=true,taskOk=true,connectOk=true;
    bool discoveryOk=true,autoHello=true,openOk=true,pollOk=true,sendOk=true;
    bool captureOk=true,playbackOk=true,playOk=true,mqtt=false;
    std::array<bool,5> busy{};
    int heapFail=-1,captureResult=3,tailResult=2;
    unsigned polls=0,connections=0,networkLive=0,captureLive=0,playbackLive=0;
    unsigned startedCapture=0,played=0,attachments=0;
    void (*worker)(void *)=nullptr;void *argument=nullptr;
    std::shared_ptr<Job> job;
    std::map<void*,size_t> allocations;
    std::map<const AudioBridge*,int> audio;
    std::vector<Event> events;
    TextCallback text;AudioCallback bytes;
    std::function<void(const char*)> hook;
    std::function<void(const ActivationCallback&,const CancelPredicate&)> discoveryHook;
};
World w;
void trace(const std::string &kind,int value=0){w.events.push_back({kind,w.clock,value});}
void fire(const char *where){if(w.hook)w.hook(where);}
size_t find(const std::string &kind,size_t from=0) {
    for(size_t i=from;i<w.events.size();++i)if(w.events[i].kind==kind)return i;
    return w.events.size();
}
size_t count(const std::string &kind) {
    return size_t(std::count_if(w.events.begin(),w.events.end(),[&](const Event &e){return e.kind==kind;}));
}
void reset(const char *name) {
    check(w.allocations.empty(),"previous scenario leaked heap");
    check(w.networkLive==0 && w.captureLive==0 && w.playbackLive==0,"previous scenario leaked I/O");
    check(core::xiaozhi::idle(),"previous scenario kept service lease");
    w=World{};scenario=name;++scenarios;
}
void text(const std::string &value){check(bool(w.text),"transport callback installed");w.text(value.data(),value.size());}
void opus(unsigned value=1){const uint8_t packet[]={uint8_t(value),9,8};check(bool(w.bytes),"audio callback installed");w.bytes(packet,sizeof(packet));}
std::string hello(const std::string &id="session-1") {
    if(w.mqtt)return "{\"type\":\"hello\",\"transport\":\"udp\",\"session_id\":\""+id+"\",\"udp\":{\"server\":\"voice.example.test\",\"port\":12345,\"key\":\"00112233445566778899aabbccddeeff\",\"nonce\":\"01000000000000000000000000000000\"}}";
    return "{\"type\":\"hello\",\"transport\":\"websocket\",\"session_id\":\""+id+"\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":60}}";
}
void create(){w.job=start();check(bool(w.job) && !idle(),"service acquired lease");check(w.worker && w.argument,"task registered");}
void run() {
    auto fn=w.worker;auto argument=w.argument;w.worker=nullptr;w.argument=nullptr;
    check(fn && argument,"worker available");fn(argument);
    check(w.job->done && idle(),"done and idle only after worker completes");
    check(w.allocations.empty() && !w.networkLive && !w.captureLive && !w.playbackLive && !w.attachments,"worker released resources");
}
void manual(std::function<void(const char*)> after) {
    int *stage=new int(0);
    auto lifetime=std::shared_ptr<int>(stage);
    w.hook=[lifetime,after](const char *where){
        if(std::strcmp(where,"poll")==0){
            const auto state=w.job->snapshot().state;
            if(*lifetime==0 && state==State::Ready){check(toggleTalk(w.job),"manual start accepted");++*lifetime;}
            else if(*lifetime==1 && state==State::Listening){check(toggleTalk(w.job),"manual stop accepted");++*lifetime;}
        }
        after(where);
    };
}
}

int64_t esp_timer_get_time(){return host::w.clock;}
void *heap_caps_malloc(size_t bytes,unsigned) {
    if(host::w.heapFail==0)return nullptr;
    if(host::w.heapFail>0)--host::w.heapFail;
    void *p=std::malloc(bytes);if(p)host::w.allocations[p]=bytes;return p;
}
void heap_caps_free(void *p) {
    if(!p)return;
    host::check(host::w.allocations.erase(p)==1,"heap free owns allocation");std::free(p);
}
int xTaskCreateWithCaps(void (*entry)(void *),const char *,unsigned stack,void *argument,unsigned priority,void *,unsigned caps) {
    host::check(stack>=40*1024 && stack<=64*1024 && priority==3 && caps==3,
                "official encoder stack budget, bounded PSRAM worker and unchanged priority");
    if(!host::w.taskOk)return 0;
    host::w.worker=entry;host::w.argument=argument;return 1;
}
void vTaskDeleteWithCaps(void *) {
    host::check(idle() && host::w.job->done,"task retirement publishes done after idle");
    host::check(host::w.allocations.empty() && !host::w.networkLive && !host::w.captureLive && !host::w.playbackLive,"task retires after all I/O");
    host::trace("task-delete");
}
namespace sys {
bool Power::isScreenOn(){return host::w.screen;}
bool Power::sleepPending(){return host::w.sleep;}
WifiState Connectivity::wifiState(){return host::w.wifi?WifiState::Connected:WifiState::Disconnected;}
}
namespace core {
void WorkerWake::attach(){++host::w.attachments;host::trace("wake-attach");}
void WorkerWake::detach(){host::check(host::w.attachments==1,"worker wake attached once");--host::w.attachments;host::trace("wake-detach");}
void WorkerWake::notify(){host::trace("notify");}
void WorkerWake::wait(uint32_t ms){host::check(ms<=20,"idle wait bounded");host::w.clock+=int64_t(ms)*1000;host::fire("wait");}
namespace recorder {bool idle(){return !host::w.busy[0];}}
namespace mp3 {bool idle(){return !host::w.busy[1];}}
namespace chip {bool idle(){return !host::w.busy[2];}}
namespace game_audio {bool idle(){return !host::w.busy[3];}}
namespace fc {bool idle(){return !host::w.busy[4];}}
}
namespace core::xiaozhi {
bool discover(CloudConfig &config,const CancelPredicate &cancelled,const ActivationCallback &activation,std::string &error) {
    host::trace("discover");
    if(host::w.discoveryHook)host::w.discoveryHook(activation,cancelled);
    if(cancelled())return false;
    config.transport=host::w.mqtt?Transport::MqttUdp:Transport::WebSocket;
    if(!host::w.discoveryOk)error="synthetic discovery failure";
    return host::w.discoveryOk;
}
TransportClient::TransportClient(TextCallback text,AudioCallback audio):onText_(std::move(text)),onAudio_(std::move(audio)){}
TransportClient::~TransportClient(){stop();}
bool TransportClient::connect(const CloudConfig &,const CancelPredicate &cancelled,std::string &error) {
    host::trace("connect");++host::w.connections;
    host::w.text=onText_;host::w.bytes=onAudio_;host::fire("connect");
    if(cancelled() || !host::w.connectOk){error="synthetic connection timeout";return false;}
    connected_=true;++host::w.networkLive;return true;
}
bool TransportClient::sendText(const std::string &value) {
    auto *json=cJSON_Parse(value.c_str());host::check(json,"outbound JSON parses");
    auto *type=cJSON_GetObjectItemCaseSensitive(json,"type");host::check(cJSON_IsString(type),"outbound type string");
    std::string kind=type->valuestring;
    if(kind=="listen"){
        auto *state=cJSON_GetObjectItemCaseSensitive(json,"state");host::check(cJSON_IsString(state),"listen state exists");kind+='-';kind+=state->valuestring;
    }
    cJSON_Delete(json);host::trace("send-"+kind);host::fire(kind.c_str());
    if(kind=="hello" && host::w.autoHello)host::text(host::hello("session-"+std::to_string(host::w.connections)));
    return host::w.sendOk;
}
bool TransportClient::sendAudio(const uint8_t *,size_t bytes,uint32_t timestamp) {
    host::trace("send-audio",int(bytes));host::trace("timestamp",int(timestamp));host::fire("send-audio");return host::w.sendOk;
}
bool TransportClient::openAudio(const ServerEvent &hello,std::string &error) {
    host::trace("audio-open",hello.sampleRate);host::fire("audio-open");
    if(!host::w.openOk)error="synthetic audio channel failure";
    return host::w.openOk;
}
bool TransportClient::pollAudio() {
    host::check(++host::w.polls<20000,"loop bounded");host::w.clock+=20000;host::fire("poll");return host::w.pollOk;
}
std::string TransportClient::error()const{return "synthetic transport failure";}
void TransportClient::stop() {
    host::trace("network-stop");
    if(connected_.exchange(false)){host::check(host::w.networkLive>0,"network live count");--host::w.networkLive;}
    host::w.text={};host::w.bytes={};host::fire("network-stop");
}
AudioBridge::~AudioBridge(){stop();}
bool AudioBridge::beginCapture() {
    host::trace("capture-begin");host::fire("capture-begin");
    if(!host::w.captureOk)return false;
    host::check(host::w.audio[this]==0,"capture/playback exclusive");host::w.audio[this]=1;++host::w.captureLive;++host::w.startedCapture;return true;
}
int AudioBridge::capture(uint8_t *p,size_t cap,unsigned &level) {
    host::check(host::w.audio[this]==1,"capture has recorder");host::trace("capture");host::w.clock+=20000;host::fire("capture");
    level=37;if(cap)std::memset(p,7,std::min(cap,size_t(3)));return host::w.captureResult;
}
int AudioBridge::finishCapture(uint8_t *p,size_t cap) {
    host::trace("capture-finish");host::fire("capture-finish");if(cap)std::memset(p,8,std::min(cap,size_t(2)));return host::w.tailResult;
}
void AudioBridge::endCapture() {
    host::trace("capture-end");
    if(host::w.audio[this]==1){host::w.audio[this]=0;--host::w.captureLive;}
}
bool AudioBridge::beginPlayback(int rate) {
    host::trace("playback-begin",rate);host::fire("playback-begin");
    if(!host::w.playbackOk)return false;
    if(host::w.audio[this]==2)return true;
    host::check(host::w.audio[this]==0,"playback/capture exclusive");host::w.audio[this]=2;++host::w.playbackLive;return true;
}
bool AudioBridge::play(const uint8_t *packet,size_t bytes) {
    host::check(host::w.audio[this]==2 && bytes,"play has output");host::trace("play",packet[0]);++host::w.played;
    host::w.clock+=60000;host::fire("play");return host::w.playOk;
}
void AudioBridge::stop() {
    host::trace("audio-stop");
    const auto it=host::w.audio.find(this);
    if(it!=host::w.audio.end()){
        if(it->second==1)--host::w.captureLive;
        if(it->second==2)--host::w.playbackLive;
        host::w.audio.erase(it);
    }
    host::fire("audio-stop");
}
}

#include "service.inc"

namespace {
using namespace host;
bool directText(Session &session,const std::string &value) {
    Session::Item item{reinterpret_cast<uint8_t*>(const_cast<char*>(value.data())),value.size(),true};return session.process(item);
}
void directReady(Session &session) {
    check(directText(session,hello()),"hello processed");check(session.state.state==State::Ready,"session ready");
}
void cancellationCases() {
    for(int mode=0;mode<4;++mode){
        reset("startup rejection");
        if(mode==0)w.screen=false;else if(mode==1)w.sleep=true;else if(mode==2)w.taskOk=false;else w.busy[0]=true;
        check(!start() && idle() && !w.worker,"failed start releases lease");
    }
    for(size_t i=0;i<w.busy.size();++i){reset("other media is busy");w.busy[i]=true;check(!start() && idle(),"each competing media rejects session");}
    reset("canceled before task dispatch");create();check(!start(),"second session rejected");cancel(w.job);run();
    check(!w.startedCapture && !w.connections && w.job->snapshot().state==State::Stopped,"prestart cancellation creates no microphone/network");
    reset("offline start");w.wifi=false;create();run();check(w.job->snapshot().state==State::Failed && !count("discover"),"offline fails before discover");
    reset("session allocation failure");create();w.heapFail=0;run();check(w.job->snapshot().state==State::Failed,"allocation failure done");
    reset("discovery failure");w.discoveryOk=false;create();run();check(w.job->snapshot().state==State::Failed && !w.connections,"discovery error stops");
    reset("discovery allocation exception");
    w.discoveryHook=[](const ActivationCallback &,const CancelPredicate &){throw std::bad_alloc();};
    create();run();check(w.job->snapshot().state==State::Failed,"discovery allocation exception releases worker");
    reset("connection failure");w.connectOk=false;create();run();check(w.job->snapshot().state==State::Failed && !w.startedCapture,"connection error no capture");
    reset("activation cancellation");
    w.discoveryHook=[](const ActivationCallback &activation,const CancelPredicate &stop){
        activation("123456","synthetic");check(w.job->snapshot().state==State::Activating && w.job->snapshot().activationCode=="123456","activation published");
        cancel(w.job);check(stop(),"discovery cancellation predicate");activation("654321","late callback");
    };
    create();run();check(w.job->snapshot().activationCode.empty() && !w.connections,"cancel clears activation and rejects late callback");
    for(int mode=0;mode<3;++mode){
        reset("ready cancellation");create();
        w.hook=[mode](const char *where){if(std::strcmp(where,"poll")==0 && w.job->snapshot().state==State::Ready){if(mode==0)cancel(w.job);else if(mode==1)w.screen=false;else w.sleep=true;}};
        run();check(w.job->snapshot().state==State::Stopped && !w.startedCapture,"ready never auto records");
    }
}
void deadlines() {
    reset("hello timeout");w.autoHello=false;create();run();
    check(w.job->snapshot().state==State::Failed && w.clock>=11000000 && w.clock<11100000 && !w.startedCapture,"ten second hello timeout");
    reset("ready idle timeout");create();run();check(w.job->snapshot().state==State::Stopped && w.clock>=121000000 && !w.startedCapture,"ready pauses after two minutes without recording");
    reset("automatic one minute recording limit");create();
    bool started=false;
    w.hook=[&](const char *where){if(std::strcmp(where,"poll")==0 && !started && w.job->snapshot().state==State::Ready){started=true;check(toggleTalk(w.job),"start long recording");}
        if(w.job->snapshot().state==State::Thinking)cancel(w.job);};
    run();check(count("send-listen-stop")==1,"recording limit stops once");
    const auto begin=find("send-listen-start"),end=find("send-listen-stop");
    check(w.events[end].at-w.events[begin].at>=60000000 && w.events[end].at-w.events[begin].at<60100000,"recording duration bounded");
    reset("reply deadline");create();manual([](const char *){});run();
    check(w.job->snapshot().state==State::Failed && w.clock>=91000000 && w.clock<91500000,"unanswered request times out");
}
void sendOrdering() {
    reset("manual conversation ordering");create();
    manual([](const char *){if(w.job->snapshot().state==State::Thinking)cancel(w.job);});run();
    check(w.startedCapture==1 && count("send-listen-start")==1 && count("send-listen-stop")==1,"one manual turn");
    const auto finish=find("capture-finish"),tail=find("send-audio",finish),close=find("capture-end",finish),stop=find("send-listen-stop");
    check(finish<tail && tail<close && close<stop,"tail frame sent before capture close and protocol stop");
    check(count("timestamp")==2 && w.events[find("timestamp")].value==0 && w.events[find("timestamp",find("timestamp")+1)].value==60,"capture packet timestamp advances sixty ms");
    reset("cancel while recording");create();
    w.hook=[](const char *where){if(std::strcmp(where,"poll")==0 && w.job->snapshot().state==State::Ready)check(toggleTalk(w.job),"record starts manually");if(std::strcmp(where,"capture")==0)cancel(w.job);};
    run();check(count("send-audio")==0 && count("capture-finish")==0 && count("send-listen-stop")==0,"canceled capture is discarded");
    reset("cancel during tail encoding");create();manual([](const char *where){if(std::strcmp(where,"capture-finish")==0)cancel(w.job);});run();
    check(count("send-audio")==1 && count("send-listen-stop")==0,"no tail sent after cancellation during encode");
    for(int code:{-1,int(MaxOpusBytes)+1}){
        reset("invalid captured packet");w.captureResult=code;create();manual([](const char*){});run();
        check(w.job->snapshot().state==State::Failed && count("send-audio")==0,"capture error or oversized packet rejects send");
    }
    reset("microphone busy after service start");w.captureOk=false;create();manual([](const char*){});run();
    check(w.job->snapshot().state==State::Failed && !count("send-listen-start"),"capture acquisition failure sends no listen");
    reset("failed text send");w.sendOk=false;create();run();check(w.job->snapshot().state==State::Failed && !w.startedCapture,"text send failure terminates");
}
void replies() {
    for(bool mqtt:{false,true}){
        reset(mqtt?"MQTT stop precedes UDP audio":"WebSocket queue drain");w.mqtt=mqtt;create();
        int stage=0;int64_t stopAt=0,lastPlayed=0;
        manual([&](const char *where){
            if(std::strcmp(where,"poll")==0){
                const auto state=w.job->snapshot().state;
                if(stage==0 && state==State::Thinking){text("{\"type\":\"stt\",\"text\":\"测试问题\"}");text("{\"type\":\"tts\",\"state\":\"start\"}");text("{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"测试回答\"}");text("{\"type\":\"tts\",\"state\":\"stop\"}");stopAt=w.clock;stage=1;}
                else if(stage==1 && w.clock-stopAt>=180000){opus(11);opus(12);stage=2;}
                else if(stage==2 && w.played==2 && w.clock-lastPlayed>=200000){opus(13);stage=3;}
                else if(stage==3 && state==State::Ready){const int64_t quiet=w.mqtt?1000000:350000;check(w.clock-lastPlayed>=quiet && w.clock-lastPlayed<quiet+100000,"transport quiet window starts after final decoded packet");check(w.job->snapshot().userText=="测试问题" && w.job->snapshot().assistantText=="测试回答","both subtitles retained");check(!w.playbackLive,"speaker released before Ready");cancel(w.job);stage=4;}
            }
            if(std::strcmp(where,"play")==0)lastPlayed=w.clock;
        });run();check(stage==4 && w.played==3 && w.startedCapture==1,"drained all delayed audio and no auto recording");
    }
    reset("MQTT 500ms and 900ms delayed full-size UDP tail");w.mqtt=true;create();
    {
        int stage=0;int64_t stopAt=0,lastPlayed=0,firstArrival=0,secondArrival=0;
        std::vector<uint8_t> tail(MaxOpusBytes,42);
        manual([&](const char *where){
            if(std::strcmp(where,"poll")==0){
                const auto state=w.job->snapshot().state;
                if(stage==0 && state==State::Thinking){
                    // This is the only queued message, so the production
                    // loop processes stop at this exact simulated time.
                    text("{\"type\":\"tts\",\"state\":\"stop\"}");stopAt=w.clock;stage=1;
                } else if(stage==1 && w.clock-stopAt>=500000){
                    check(state!=State::Ready,"500ms tail arrives before response retires");firstArrival=w.clock-stopAt;
                    w.bytes(tail.data(),tail.size());stage=2;
                } else if(stage==2 && w.clock-stopAt>=900000){
                    check(state!=State::Ready,"900ms tail arrives before response retires");secondArrival=w.clock-stopAt;
                    tail[0]=43;w.bytes(tail.data(),tail.size());tail[0]=44;w.bytes(tail.data(),tail.size());stage=3;
                } else if(stage==3 && state==State::Ready){
                    check(w.played==3,"all full-size tail packets drain before Ready");
                    check(w.clock-lastPlayed>=1000000 && w.clock-lastPlayed<1100000,"one full second after final drained packet");
                    check(!w.playbackLive,"speaker closes before Ready");cancel(w.job);stage=4;
                }
                if(stage>0 && stage<3)check(state!=State::Ready,"reply stays active while late packets are outstanding");
            }
            if(std::strcmp(where,"play")==0)lastPlayed=w.clock;
        });
        run();check(stage==4 && w.played==3 && w.startedCapture==1,"delayed MQTT response finished without auto record");
        check(firstArrival>=500000 && firstArrival<540000 && secondArrival>=900000 && secondArrival<940000,"tail injection at 500ms and 900ms with bounded scheduler granularity");
    }
    for(int mode=0;mode<3;++mode){
        reset("reply error resource cleanup");create();bool sent=false;
        manual([&](const char *where){if(!sent && std::strcmp(where,"poll")==0 && w.job->snapshot().state==State::Thinking){sent=true;if(mode==0)w.playbackOk=false;if(mode==1)w.playOk=false;if(mode==2)w.pollOk=false;opus();}});
        run();check(w.job->snapshot().state==State::Failed,"reply error publishes failure");
    }
    reset("Wi-Fi disconnect");create();bool changed=false;
    w.hook=[&](const char *where){if(!changed && std::strcmp(where,"poll")==0 && w.job->snapshot().state==State::Ready){changed=true;w.wifi=false;}};
    run();check(w.job->snapshot().state==State::Failed && !w.startedCapture,"disconnect releases session");
    reset("abort queued reply and reconnect");create();int stage=0;
    manual([&](const char *where){
        if(std::strcmp(where,"poll")==0){
            const auto state=w.job->snapshot().state;
            if(stage==0 && state==State::Thinking){text("{\"type\":\"tts\",\"state\":\"start\"}");opus(21);opus(22);stage=1;}
            else if(stage==1 && state==State::Speaking){check(toggleTalk(w.job),"abort accepted");check(!toggleTalk(w.job),"pending abort debounced");stage=2;}
            else if(stage==2 && w.connections==2 && state==State::Ready){check(w.startedCapture==1 && w.played==0,"old queued audio discarded and fresh session never auto records");cancel(w.job);stage=3;}
        }
        if(std::strcmp(where,"abort")==0){check(!w.playbackLive && !w.captureLive,"abort closes audio before network I/O");opus(23);}
    });run();
    check(stage==3 && w.connections==2 && count("send-abort")==1 && count("send-hello")==2,"abort reconnected with fresh hello");
    const auto abort=find("send-abort");check(abort>0 && w.events[abort-1].kind=="audio-stop","speaker stop ordered immediately before abort send");
}
void queueAndInput() {
    reset("bounded audio and control queues");
    {auto job=std::make_shared<Job>();Session session(job);std::vector<uint8_t> audio(MaxOpusBytes,1);
        check(MaxOpusBytes==4096 && MaxOpusBytes==AudioBridge::PacketCapacity,"4096-byte multi-frame packet cap matches bridge");
        for(unsigned i=0;i<256;++i)session.receive(audio.data(),audio.size(),false);
        check(session.audioCount==256 && !session.inboxError,"audio cap admitted");
        size_t allocated=0;for(const auto &allocation:w.allocations)allocated+=allocation.second;
        check(allocated==256*4096,"full audio queue bounded to one MiB of payload");
        const std::string json="{}";for(unsigned i=0;i<16;++i)session.receive(json.data(),json.size(),true);
        check(session.count==272 && !session.inboxError,"control headroom retained with full audio queue");
        session.receive(audio.data(),audio.size(),false);check(session.inboxError && session.count==272,"audio cap rejects overflow");
        session.clearInbox();check(session.empty() && !session.audioCount && !session.textCount && !session.textBytes && !session.inboxError,"drain resets all counters");
        session.accepting=false;session.receive(audio.data(),audio.size(),false);check(session.empty(),"stopped callback ignored");
    }
    reset("bounded text bytes");
    {auto job=std::make_shared<Job>();Session session(job);std::string payload(MaxJsonBytes,' ');
        for(unsigned i=0;i<4;++i)session.receive(payload.data(),payload.size(),true);
        check(session.textBytes==65536 && !session.inboxError,"text byte cap admitted");
        session.receive(payload.data(),1,true);check(session.inboxError && session.textCount==4,"byte cap rejects overflow before item cap");
    }
    for(int mode=0;mode<5;++mode){
        reset("receive rejects invalid or failed allocation");
        {auto job=std::make_shared<Job>();Session session(job);std::vector<uint8_t> buffer(MaxJsonBytes+1,1);
            if(mode==0)session.receive(nullptr,1,false);
            if(mode==1)session.receive(buffer.data(),0,false);
            if(mode==2)session.receive(buffer.data(),MaxOpusBytes+1,false);
            if(mode==3)session.receive(buffer.data(),MaxJsonBytes+1,true);
            if(mode==4){w.heapFail=0;session.receive(buffer.data(),1,false);}
            check(session.inboxError && session.empty(),"bad receive safely fails");
        }
    }
    reset("canceled callbacks ignored");
    {auto job=std::make_shared<Job>();Session session(job);cancel(job);session.receive(nullptr,1,false);check(!session.inboxError && session.empty(),"cancel rejects callback before allocation");}
    reset("strict session and message handling");
    {auto job=std::make_shared<Job>();Session session(job);directReady(session);
        check(directText(session,hello()) && session.state.state==State::Ready,"duplicate same hello tolerated");
        check(directText(session,"{\"type\":\"tts\",\"state\":\"start\"}") && session.state.state==State::Ready,"unsolicited TTS ignored");
        check(directText(session,"{\"type\":\"system\",\"command\":\"reboot\"}") && session.state.state==State::Ready,"system command is not executed");
        session.reply=true;session.transition(State::Thinking);
        check(directText(session,"{\"type\":\"stt\",\"session_id\":\"other\",\"text\":\"old\"}") && session.state.userText.empty(),"different session transcript ignored");
        check(!directText(session,hello("other")) && session.state.state==State::Failed,"changed hello requires reconnect");
    }
    reset("malformed JSON terminates safely");
    {auto job=std::make_shared<Job>();Session session(job);check(!directText(session,"{\"type\":") && session.state.state==State::Failed,"malformed message failure");}
    reset("popped item freed on exception");create();
    w.hook=[](const char *where){if(std::strcmp(where,"audio-open")==0)throw std::bad_alloc();};
    run();check(w.job->snapshot().state==State::Failed && w.allocations.empty(),"exception frees popped item and session");
    reset("callback queue allocation failure terminates worker");create();
    w.hook=[](const char *where){if(std::strcmp(where,"hello")==0)w.heapFail=0;};
    run();check(w.job->snapshot().state==State::Failed && !w.startedCapture,"queue allocation failure releases worker");
    reset("UTF8 subtitle limit");
    {std::string output(MaxTextBytes-3,'a');appendText(output,"你们好");check(output.size()==MaxTextBytes-2,"no partial code point at text cap");
        output=std::string(MaxTextBytes-4,'a');appendText(output,"你们好");check(output.size()==MaxTextBytes && output.substr(MaxTextBytes-3)=="你","one complete code point fits");
        appendText(output,"追加");check(output.size()==MaxTextBytes,"subtitle remains bounded");}
    reset("public command state checks");
    {auto job=std::make_shared<Job>();Session session(job);
        for(auto state:{State::Connecting,State::Activating,State::Thinking,State::Failed,State::Stopped}){session.transition(state);check(!toggleTalk(job),"unavailable state rejects talk");}
        session.transition(State::Ready);check(toggleTalk(job) && !toggleTalk(job),"pending command rejects duplicate");session.command();
        job->done=true;check(!toggleTalk(job),"done job rejects talk");job->done=false;cancel(job);check(!toggleTalk(job) && !toggleTalk({}),"cancelled or null rejects talk");
    }
}
void cancellationBoundaries() {
    reset("cancel delivered by poll before start command");create();bool triggered=false;
    w.hook=[&](const char *where){if(!triggered && std::strcmp(where,"poll")==0 && w.job->snapshot().state==State::Ready){triggered=true;check(toggleTalk(w.job),"queued manual start");cancel(w.job);}};
    run();check(w.startedCapture==0,"canceled queued start must not open microphone");
    reset("canceled direct reply cannot open speaker");
    {auto job=std::make_shared<Job>();Session session(job);directReady(session);session.reply=true;session.transition(State::Thinking);
        cancel(job);uint8_t bytes[]={1,2};Session::Item item{bytes,sizeof(bytes),false};check(!session.process(item),"canceled processing ends conversation");check(w.played==0 && !w.playbackLive,"canceled item cannot start playback");}
    reset("cancel between speaker acquisition and play");
    {auto job=std::make_shared<Job>();Session session(job);directReady(session);session.reply=true;session.transition(State::Thinking);
        w.hook=[job](const char *where){if(std::strcmp(where,"playback-begin")==0)cancel(job);};
        uint8_t bytes[]={1,2};Session::Item item{bytes,sizeof(bytes),false};check(!session.process(item),"cancel after output acquisition ends processing");check(w.played==0,"canceled output never receives a packet");}
    reset("canceled direct start and stop cannot capture");
    {auto job=std::make_shared<Job>();Session session(job);directReady(session);cancel(job);
        check(!session.startTalking() && !w.startedCapture,"canceled Ready cannot start capture");session.transition(State::Listening);
        check(!session.stopTalking() && !count("capture-finish"),"canceled Listening discards final frame");}
}
}
int main() {
    cancellationCases();deadlines();sendOrdering();replies();queueAndInput();cancellationBoundaries();
    host::check(host::w.allocations.empty() && idle(),"final resource state clean");
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"production_service\":true,\"production_protocol\":true}\n",host::scenarios,host::checks);
}
