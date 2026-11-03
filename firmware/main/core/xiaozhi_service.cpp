#include "xiaozhi_service.hpp"
#include "xiaozhi_audio.hpp"
#include "xiaozhi_cloud.hpp"
#include "xiaozhi_transport.hpp"
#include "audio_recorder.hpp"
#include "mp3_player.hpp"
#include "chiptune_player.hpp"
#include "game_audio.hpp"
#include "fc_emulator.hpp"
#include "connectivity.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>

namespace core::xiaozhi {
namespace {
std::atomic<bool> active{false};
constexpr char Tag[]="xiaozhi";
// ESP Audio Codec 2.5.0 recommends about 40 KiB for a task supporting all
// its encoders; this is a safety budget, not a measured Opus-only peak.
// Its shipped S3 silk_encode_frame_FIX alone reserves 9,520 bytes, before
// nested/dynamic scratch and our service frames: the old 8 KiB task overflows
// on the first captured Opus frame. Keep 8 KiB for service/network overhead;
// allocate the whole bounded stack in PSRAM, as with the session buffers.
constexpr unsigned WorkerStackBytes=48*1024;
constexpr size_t AudioQueueLimit=256, TextQueueLimit=16;
constexpr size_t QueueCapacity=AudioQueueLimit+TextQueueLimit;
constexpr size_t TextByteLimit=64*1024;
constexpr int64_t Second=1000000;
static_assert(MaxOpusBytes==AudioBridge::PacketCapacity,
              "Xiaozhi transport and audio packet budgets must match");
int64_t now() {return esp_timer_get_time();}

// Truncate only at a UTF-8 boundary; never give LVGL an incomplete code point.
void appendText(std::string &dest,const std::string &text) {
    if(dest.size()>=MaxTextBytes || text.empty())return;
    if(!dest.empty())dest+='\n';
    size_t count=std::min(MaxTextBytes-dest.size(),text.size());
    while(count<text.size() && count && (uint8_t(text[count])&0xc0)==0x80)--count;
    dest.append(text.data(),count);
}
}

// A Session belongs to one worker. SDK callbacks can only put bounded messages
// into its inbox. No callback touches I2S, LVGL or a subsequent Session.
struct Session {
    explicit Session(std::shared_ptr<Job> j):job(std::move(j)),
        network([this](const char *p,size_t n){receive(p,n,true);},
                [this](const uint8_t *p,size_t n){receive(p,n,false);}) {}
    ~Session() {network.stop();audio.stop();clearInbox();}
    struct Item {uint8_t *data=nullptr;size_t size=0;bool text=false;};
    std::shared_ptr<Job> job;
    TransportClient network;
    AudioBridge audio;
    CloudConfig config;
    Snapshot state;
    std::string sessionId;
    std::array<Item,QueueCapacity> inbox{};
    size_t head=0,count=0,audioCount=0,textCount=0,textBytes=0;
    std::mutex inboxMutex;
    std::atomic<bool> accepting{true},inboxError{false};
    uint8_t packet[AudioBridge::PacketCapacity]{};
    bool hello=false,reply=false,ttsEnded=false;
    int sampleRate=24000;
    int64_t stateAt=0,lastAudio=0,endedAt=0,lastInbound=0;
    uint32_t timestamp=0;

    bool cancelled() const {
        return job->cancelled.load() || sys::Power::sleepPending() || !sys::Power::isScreenOn();
    }
    bool online() const {return sys::Connectivity::wifiState()==sys::WifiState::Connected;}
    void publish() {
        std::lock_guard<std::mutex> lock(job->mutex_);
        state.revision=job->published_.revision+1;
        job->published_=state;
    }
    void transition(State next,const char *notice="") {
        state.state=next;state.notice=notice;state.level=0;stateAt=now();publish();
    }
    void fail(const std::string &message) {
        reply=false;state.activationCode.clear();transition(State::Failed,message.c_str());
    }
    void receive(const void *source,size_t bytes,bool text) noexcept {
        if(!accepting.load() || cancelled())return;
        if(!source || !bytes || bytes>(text?MaxJsonBytes:MaxOpusBytes)) {
            inboxError=true;job->wake_.notify();return;
        }
        {
            std::lock_guard<std::mutex> lock(inboxMutex);
            if(!accepting.load())return;
            if(count>=QueueCapacity || (text?(textCount>=TextQueueLimit || textBytes+bytes>TextByteLimit):audioCount>=AudioQueueLimit)) {
                inboxError=true;
            } else {
                auto data=static_cast<uint8_t *>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
                if(!data)inboxError=true;
                else {
                    std::memcpy(data,source,bytes);
                    inbox[(head+count)%QueueCapacity]={data,bytes,text};++count;
                    if(text){++textCount;textBytes+=bytes;}else ++audioCount;
                }
            }
        }
        job->wake_.notify();
    }
    bool pop(Item &item) {
        std::lock_guard<std::mutex> lock(inboxMutex);
        if(!count)return false;
        item=inbox[head];inbox[head]={};head=(head+1)%QueueCapacity;--count;
        if(item.text){--textCount;textBytes-=item.size;}else --audioCount;
        return true;
    }
    bool empty() {
        std::lock_guard<std::mutex> lock(inboxMutex);return count==0;
    }
    void clearInbox() {
        Item item;
        while(pop(item))heap_caps_free(item.data);
        inboxError=false;
    }
    Job::Command command() {
        std::lock_guard<std::mutex> lock(job->mutex_);
        auto result=job->command_;job->command_=Job::Command::None;return result;
    }
    bool sendText(const std::string &text) {
        if(cancelled())return false;
        if(text.empty() || !network.sendText(text)) {
            fail("发送失败，请重试");return false;
        }
        return !cancelled();
    }
    bool sendPacket(int size) {
        if(cancelled())return false;
        if(size<0){fail("麦克风读取失败");return false;}
        if(!size)return true;
        if(size>int(MaxOpusBytes) || !network.sendAudio(packet,size,timestamp)) {
            fail("语音发送失败，请重试");return false;
        }
        timestamp+=60;
        return !cancelled();
    }
    bool startTalking() {
        if(cancelled())return false;
        if(state.state!=State::Ready)return true;
        reply=false;ttsEnded=false;timestamp=0;
        state.userText.clear();state.assistantText.clear();
        if(!audio.beginCapture()){fail("麦克风忙，请稍后重试");return false;}
        if(!sendText(makeListen(sessionId,true)))return false;
        transition(State::Listening);return true;
    }
    bool stopTalking() {
        if(cancelled())return false;
        if(state.state!=State::Listening)return true;
        if(!sendPacket(audio.finishCapture(packet,sizeof(packet))))return false;
        audio.endCapture();reply=true;ttsEnded=false;
        lastAudio=endedAt=0;
        if(!sendText(makeListen(sessionId,false)))return false;
        transition(State::Thinking);return true;
    }
    bool process(Item &item) {
        if(cancelled())return false;
        lastInbound=now();
        if(!item.text) {
            if(!hello || !reply || (state.state!=State::Thinking && state.state!=State::Speaking))return true;
            if(!audio.beginPlayback(sampleRate) || cancelled() || !audio.play(item.data,item.size)) {
                if(!cancelled())fail("语音播放失败，请重试");
                return false;
            }
            lastAudio=now();
            if(state.state!=State::Speaking)transition(State::Speaking);
            return true;
        }
        ServerEvent event;std::string error;
        if(!parseServerEvent(reinterpret_cast<char *>(item.data),item.size,config.transport,event,error)) {
            fail(error.empty()?"服务数据不完整，请重试":error);return false;
        }
        if(event.type==EventType::Hello) {
            if(hello) {
                if(event.sessionId==sessionId)return true;
                fail("会话已变更，请重新连接");return false;
            }
            if(!network.openAudio(event,error)) {
                fail(error.empty()?"语音通道连接失败":error);return false;
            }
            sessionId=event.sessionId;sampleRate=event.sampleRate;hello=true;
            state.activationCode.clear();transition(State::Ready);return true;
        }
        if(!hello || (!event.sessionId.empty() && event.sessionId!=sessionId))return true;
        switch(event.type) {
        case EventType::Goodbye:
            transition(State::Stopped,"会话已结束，点击连接");return false;
        case EventType::Alert:
            fail(event.text.empty()?"小智服务暂不可用":event.text);return false;
        case EventType::Stt:
            if(reply || state.state==State::Listening){state.userText=event.text;publish();}
            break;
        case EventType::TtsStart:
            if(reply){ttsEnded=false;transition(State::Speaking);}
            break;
        case EventType::TtsSentence:
            if(reply){appendText(state.assistantText,event.text);publish();}
            break;
        case EventType::TtsStop:
            if(reply){ttsEnded=true;endedAt=now();}
            break;
        default:break;
        }
        return true;
    }
    // True requests a fresh connection after an explicit interrupt. A new
    // server hello/session prevents queued audio from the aborted turn leaking
    // into a subsequent recording. Never auto-record after reconnecting.
    bool conversation() {
        hello=reply=ttsEnded=false;lastInbound=now();stateAt=now();
        sessionId.clear();accepting=true;
        std::string error;
        if(!network.connect(config,[this]{return cancelled() || !online();},error)) {
            if(!cancelled())fail(error.empty()?"连接失败，请重试":error);
            return false;
        }
        if(!sendText(makeHello(config.transport,config.protocolVersion)))return false;
        const int64_t helloDeadline=now()+10*Second;
        while(!cancelled()) {
            if(!online()){fail("Wi-Fi已断开");break;}
            if(!network.pollAudio() || !network.connected()) {
                const auto reason=network.error();
                fail(reason.empty()?"连接已断开，请重试":reason);break;
            }
            // Poll/SDK callbacks can race with a foreground cancel. Recheck
            // before consuming an old command or opening either audio path.
            if(cancelled())break;
            if(inboxError.load()){fail("语音缓冲不足，请重试");break;}
            const auto cmd=command();
            if(cmd==Job::Command::Abort && state.state==State::Speaking) {
                // Stop speaker first, before any potentially blocking network I/O.
                audio.stop();reply=false;accepting=false;
                if(!sendText(makeAbort(sessionId)))return false;
                transition(State::Connecting,"正在重新连接");return true;
            }
            if(cmd==Job::Command::Start && !startTalking())break;
            if((cmd==Job::Command::Stop ||
               (state.state==State::Listening && now()-stateAt>=60*Second)) && !stopTalking())break;
            Item item;
            if(pop(item)) {
                bool ok=false;
                try {ok=process(item);} catch(...) {heap_caps_free(item.data);throw;}
                heap_caps_free(item.data);
                if(!ok)break;
            }
            if(cancelled())break;
            if(!hello && now()>=helloDeadline){fail("小智响应超时，请重试");break;}
            if(state.state==State::Listening) {
                unsigned level=0;
                if(!sendPacket(audio.capture(packet,sizeof(packet),level)))break;
                if(level!=state.level){state.level=level;publish();}
            } else if(reply && ttsEnded && empty() &&
                      now()-std::max(lastAudio,endedAt)>=
                          (config.transport==Transport::MqttUdp?Second:350000)) {
                // MQTT control and UDP audio are independent channels. Wait a
                // full second after stop AND the final local audio packet so
                // delayed UDP tail packets can arrive. Ordered WebSocket
                // delivery retains its shorter 350-ms quiet window.
                audio.stop();reply=false;ttsEnded=false;transition(State::Ready);
            } else if(reply && now()-std::max(stateAt,lastAudio)>=90*Second) {
                fail("回复等待超时，请重试");break;
            } else if(hello && state.state==State::Ready && now()-lastInbound>=120*Second) {
                transition(State::Stopped,"会话已暂停，点击连接");break;
            }
            if(empty() && state.state!=State::Listening)job->wake_.wait(20);
        }
        return false;
    }
    void run() {
        job->wake_.attach();
        try {
            if(!online())fail("请先连接Wi-Fi");
            else {
                transition(State::Connecting);
                std::string error;
                const bool found=discover(config,[this]{return cancelled();},
                    [this](const std::string &code,const std::string &) {
                        if(cancelled())return;
                        state.activationCode=code;transition(State::Activating,"请在小智平台绑定设备");
                    },error);
                if(found && !cancelled()) {
                    state.activationCode.clear();transition(State::Connecting);
                    bool again;
                    do {
                        again=conversation();accepting=false;
                        audio.stop();network.stop();clearInbox();
                    } while(again && !cancelled());
                } else if(!cancelled())fail(error.empty()?"连接失败，请重试":error);
            }
        } catch(const std::bad_alloc &) {
            // Use a fixed short message; never format secrets or cloud contents.
            try {fail("内存不足，请重试");} catch(...) {}
        }
        accepting=false;
        audio.stop();network.stop();clearInbox();
        if(cancelled()) {
            state.activationCode.clear();state.userText.clear();state.assistantText.clear();
            try {transition(State::Stopped,"点击连接继续");} catch(...) {}
        }
        job->wake_.detach();
    }
    static void worker(void *argument) {
        ESP_LOGI(Tag,"worker ready: stack=%u free_min=%u",WorkerStackBytes,
                 unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        auto holder=static_cast<std::shared_ptr<Job> *>(argument);
        auto job=std::move(*holder);delete holder;
        auto memory=heap_caps_malloc(sizeof(Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        Session *session=nullptr;
        try {
            if(!memory)throw std::bad_alloc();
            session=new(memory) Session(job);
            session->run();session->~Session();session=nullptr;
        } catch(const std::bad_alloc &) {
            if(session)session->~Session();
            std::lock_guard<std::mutex> lock(job->mutex_);
            job->published_.state=State::Failed;
            ++job->published_.revision;
        }
        heap_caps_free(memory);
        ESP_LOGI(Tag,"worker stopped: stack free_min=%u",
                 unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        // All callbacks, sockets and codec resources have ceased before idle
        // allows another App to acquire audio or the OS to turn off radios.
        active.store(false,std::memory_order_release);
        job->done.store(true,std::memory_order_release);
        job.reset();
        vTaskDeleteWithCaps(nullptr);
    }
};

Snapshot Job::snapshot() {std::lock_guard<std::mutex> lock(mutex_);return published_;}
std::shared_ptr<Job> start() {
    if(sys::Power::sleepPending() || !sys::Power::isScreenOn() ||
       !recorder::idle() || !mp3::idle() || !chip::idle() || !game_audio::idle() || !fc::idle() || active.exchange(true))return {};
    std::shared_ptr<Job> job;
    std::shared_ptr<Job> *holder=nullptr;
    try {job=std::make_shared<Job>();holder=new std::shared_ptr<Job>(job);}
    catch(const std::bad_alloc &) {active=false;return {};}
    if(xTaskCreateWithCaps(Session::worker,"xiaozhi",WorkerStackBytes,holder,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        delete holder;active=false;return {};
    }
    return job;
}
bool toggleTalk(const std::shared_ptr<Job> &job) {
    if(!job || job->cancelled.load() || job->done.load() || sys::Power::sleepPending() || !sys::Power::isScreenOn())return false;
    {
        std::lock_guard<std::mutex> lock(job->mutex_);
        if(job->command_!=Job::Command::None)return false;
        switch(job->published_.state) {
        case State::Ready:job->command_=Job::Command::Start;break;
        case State::Listening:job->command_=Job::Command::Stop;break;
        case State::Speaking:job->command_=Job::Command::Abort;break;
        default:return false;
        }
    }
    job->wake_.notify();return true;
}
void cancel(const std::shared_ptr<Job> &job) {
    if(job){job->cancelled=true;job->wake_.notify();}
}
bool idle() {return !active.load(std::memory_order_acquire);}
} // namespace core::xiaozhi
